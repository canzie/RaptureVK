#include "text/TextBuffer.h"

#include <algorithm>
#include <cstring>

// Children per index block. Chosen so three levels address more blocks than a document can
// hold, keeping a cold descent at three steps.
static constexpr size_t INDEX_FANOUT = 128;

bool TextBuffer::TextBlock::fits(size_t lineBytes) const
{
    size_t needed = lineBytes + sizeof(uint32_t);
    size_t used = lineStarts.size() * sizeof(uint32_t) + usedBytes();
    return used + needed <= capacity;
}

std::string_view TextBuffer::TextBlock::line(uint32_t local) const
{
    uint32_t start = lineStarts[local];
    uint32_t stop = (local == 0) ? capacity : lineStarts[local - 1];
    return std::string_view(bytes.data() + start, stop - start);
}

TextBuffer::TextBuffer()
{
    load({});
}

TextBuffer::TextBuffer(std::string_view text)
{
    load(text);
}

void TextBuffer::load(std::string_view text)
{
    m_textBlocks.clear();
    m_indexBlocks.clear();
    m_root = INVALID;
    m_lineCount = 0;
    m_cachedBlock = INVALID;

    uint64_t carriageReturns = 0;
    size_t cursor = 0;
    while (true) {
        size_t newline = text.find('\n', cursor);
        size_t stop = (newline == std::string_view::npos) ? text.size() : newline;
        std::string_view raw = text.substr(cursor, stop - cursor);
        if (!raw.empty() && raw.back() == '\r') {
            raw.remove_suffix(1);
            carriageReturns++;
        }
        appendLine(raw);

        if (newline == std::string_view::npos) {
            break;
        }
        cursor = newline + 1;
    }

    // A quarter is enough to call it, so a file with a few stray endings still round-trips
    // as what it mostly is rather than being rewritten wholesale.
    m_lineEnding = (carriageReturns > m_lineCount / 4 && carriageReturns > 0) ? LineEnding::CRLF : LineEnding::LF;

    buildIndex();
    m_revision++;
}

void TextBuffer::appendLine(std::string_view text)
{
    if (m_textBlocks.empty() || !m_textBlocks.back().fits(text.size())) {
        uint32_t needed = static_cast<uint32_t>(text.size() + sizeof(uint32_t));
        TextBlock block;
        block.capacity = std::max(BLOCK_BYTES, needed);
        block.textStart = block.capacity;
        block.bytes.resize(block.capacity);
        m_textBlocks.push_back(std::move(block));
    }

    TextBlock &block = m_textBlocks.back();
    block.textStart -= static_cast<uint32_t>(text.size());
    std::memcpy(block.bytes.data() + block.textStart, text.data(), text.size());
    block.lineStarts.push_back(block.textStart);
    m_lineCount++;
}

uint64_t TextBuffer::subtreeLineCount(uint32_t id) const
{
    if (isTextBlock(id)) {
        return m_textBlocks[blockIndex(id)].lineCount();
    }

    uint64_t total = 0;
    for (uint64_t count : m_indexBlocks[blockIndex(id)].lineCounts) {
        total += count;
    }
    return total;
}

void TextBuffer::setParent(uint32_t id, uint32_t parent)
{
    if (isTextBlock(id)) {
        m_textBlocks[blockIndex(id)].parent = parent;
    } else {
        m_indexBlocks[blockIndex(id)].parent = parent;
    }
}

void TextBuffer::buildIndex()
{
    m_indexBlocks.clear();
    m_cachedBlock = INVALID;

    std::vector<uint32_t> level;
    level.reserve(m_textBlocks.size());
    for (uint32_t i = 0; i < m_textBlocks.size(); i++) {
        m_textBlocks[i].parent = INVALID;
        level.push_back(i | TEXT_BLOCK_FLAG);
    }

    while (level.size() > 1) {
        std::vector<uint32_t> next;
        next.reserve(level.size() / INDEX_FANOUT + 1);

        for (size_t i = 0; i < level.size(); i += INDEX_FANOUT) {
            size_t stop = std::min(i + INDEX_FANOUT, level.size());

            IndexBlock index;
            index.children.assign(level.begin() + i, level.begin() + stop);
            index.lineCounts.reserve(index.children.size());
            for (uint32_t child : index.children) {
                index.lineCounts.push_back(subtreeLineCount(child));
            }

            m_indexBlocks.push_back(std::move(index));
            uint32_t id = static_cast<uint32_t>(m_indexBlocks.size() - 1);
            for (uint32_t child : m_indexBlocks[id].children) {
                setParent(child, id);
            }
            next.push_back(id);
        }
        level = std::move(next);
    }

    m_root = level.empty() ? INVALID : level[0];
}

uint32_t TextBuffer::textBlockForLine(uint64_t index) const
{
    if (index >= m_lineCount) {
        return INVALID;
    }
    if (m_cachedBlock != INVALID && index >= m_cachedFirstLine && index < m_cachedEndLine) {
        return m_cachedBlock;
    }

    uint64_t base = 0;
    uint32_t id = m_root;
    while (!isTextBlock(id)) {
        const IndexBlock &node = m_indexBlocks[blockIndex(id)];
        uint32_t chosen = node.children.back();
        for (size_t i = 0; i < node.children.size(); i++) {
            if (index < base + node.lineCounts[i]) {
                chosen = node.children[i];
                break;
            }
            base += node.lineCounts[i];
        }
        id = chosen;
    }

    uint32_t block = blockIndex(id);
    m_cachedBlock = block;
    m_cachedFirstLine = base;
    m_cachedEndLine = base + m_textBlocks[block].lineCount();
    return block;
}

std::string_view TextBuffer::line(uint64_t index) const
{
    uint32_t block = textBlockForLine(index);
    if (block == INVALID) {
        return {};
    }
    return m_textBlocks[block].line(static_cast<uint32_t>(index - m_cachedFirstLine));
}

uint64_t TextBuffer::lineLength(uint64_t index) const
{
    return line(index).size();
}

std::string TextBuffer::text() const
{
    std::string_view terminator = (m_lineEnding == LineEnding::CRLF) ? "\r\n" : "\n";

    std::string out;
    for (uint64_t i = 0; i < m_lineCount; i++) {
        out.append(line(i));
        if (i + 1 < m_lineCount) {
            out.append(terminator);
        }
    }
    return out;
}

void TextBuffer::propagateLineCount(uint32_t textBlock, int64_t delta)
{
    if (delta == 0) {
        return;
    }

    uint32_t child = textBlock | TEXT_BLOCK_FLAG;
    uint32_t parent = m_textBlocks[textBlock].parent;
    while (parent != INVALID) {
        IndexBlock &node = m_indexBlocks[parent];
        for (size_t i = 0; i < node.children.size(); i++) {
            if (node.children[i] == child) {
                node.lineCounts[i] = static_cast<uint64_t>(static_cast<int64_t>(node.lineCounts[i]) + delta);
                break;
            }
        }
        child = parent;
        parent = node.parent;
    }
}

std::vector<TextBuffer::TextBlock> TextBuffer::packLines(const std::vector<std::string> &lines)
{
    std::vector<TextBlock> packed;

    for (const std::string &text : lines) {
        if (packed.empty() || !packed.back().fits(text.size())) {
            uint32_t needed = static_cast<uint32_t>(text.size() + sizeof(uint32_t));
            TextBlock block;
            block.capacity = std::max(BLOCK_BYTES, needed);
            block.textStart = block.capacity;
            block.bytes.resize(block.capacity);
            packed.push_back(std::move(block));
        }

        TextBlock &block = packed.back();
        block.textStart -= static_cast<uint32_t>(text.size());
        std::memcpy(block.bytes.data() + block.textStart, text.data(), text.size());
        block.lineStarts.push_back(block.textStart);
    }

    return packed;
}

void TextBuffer::spliceLines(uint64_t first, uint64_t count, const std::vector<std::string> &replacement)
{
    uint32_t firstBlock = textBlockForLine(first);
    uint64_t firstBlockLine = m_cachedFirstLine;
    uint32_t lastBlock = textBlockForLine(first + count - 1);

    // Blocks sit in document order, so the touched span is contiguous.
    std::vector<std::string> lines;
    for (uint32_t block = firstBlock; block <= lastBlock; block++) {
        const TextBlock &source = m_textBlocks[block];
        for (uint32_t local = 0; local < source.lineCount(); local++) {
            lines.emplace_back(source.line(local));
        }
    }

    size_t offset = static_cast<size_t>(first - firstBlockLine);
    lines.erase(lines.begin() + offset, lines.begin() + offset + count);
    lines.insert(lines.begin() + offset, replacement.begin(), replacement.end());

    std::vector<TextBlock> packed = packLines(lines);
    uint32_t oldBlockCount = lastBlock - firstBlock + 1;

    m_lineCount = m_lineCount - count + replacement.size();
    m_cachedBlock = INVALID;

    if (packed.size() == oldBlockCount) {
        for (uint32_t i = 0; i < oldBlockCount; i++) {
            uint32_t target = firstBlock + i;
            int64_t before = m_textBlocks[target].lineCount();
            uint32_t parent = m_textBlocks[target].parent;
            m_textBlocks[target] = std::move(packed[i]);
            m_textBlocks[target].parent = parent;
            propagateLineCount(target, static_cast<int64_t>(m_textBlocks[target].lineCount()) - before);
        }
        return;
    }

    // The span no longer packs into the same number of blocks, so the tree above it changes
    // shape. Rebuilding the index is O(blocks) and happens only when a block splits or merges.
    m_textBlocks.erase(m_textBlocks.begin() + firstBlock, m_textBlocks.begin() + lastBlock + 1);
    m_textBlocks.insert(m_textBlocks.begin() + firstBlock, std::make_move_iterator(packed.begin()),
                        std::make_move_iterator(packed.end()));
    buildIndex();
}

void TextBuffer::replace(Amethyst::TextRange range, std::string_view with)
{
    if (m_lineCount == 0) {
        return;
    }

    uint64_t startLine = std::min(range.start.line, m_lineCount - 1);
    uint64_t endLine = std::min(range.end.line, m_lineCount - 1);
    if (endLine < startLine) {
        std::swap(startLine, endLine);
    }

    std::string prefix(line(startLine).substr(0, std::min<size_t>(range.start.column, line(startLine).size())));
    std::string_view endText = line(endLine);
    std::string suffix(endText.substr(std::min<size_t>(range.end.column, endText.size())));

    std::vector<std::string> lines;
    std::string current = std::move(prefix);
    size_t cursor = 0;
    while (true) {
        size_t newline = with.find('\n', cursor);
        size_t stop = (newline == std::string_view::npos) ? with.size() : newline;
        current.append(with.substr(cursor, stop - cursor));

        if (newline == std::string_view::npos) {
            break;
        }
        lines.push_back(std::move(current));
        current.clear();
        cursor = newline + 1;
    }
    current.append(suffix);
    lines.push_back(std::move(current));

    spliceLines(startLine, endLine - startLine + 1, lines);
    m_revision++;
}

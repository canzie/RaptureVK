// Text document benchmark: flat line-offset array vs a block tree.
//
// Two candidate structures for a text editor's document layer, measured on the access mix a
// viewport-driven editor actually has: load a file, scan the visible lines every frame, jump
// somewhere, occasionally edit.
//
//   Flat  - the whole file in one buffer plus a vector of line-start offsets. What the RAD
//           debugger builds (TXT_TextInfo::lines_ranges), except RAD stores a range per line
//           where one offset suffices.
//   Tree  - a B+tree of 4096-byte blocks. Leaves hold line text at the end growing backwards
//           with a u32 offset index at the front growing forwards; branches hold per-child
//           line counts, so no global line table exists. Vim's memline, minus the swapfile,
//           recovery and marks.
//
// Deliberately not wired into CMake - delete tools/ and nothing notices:
//
//   c++ -O2 -std=c++20 tools/text_document_bench.cpp -o /tmp/text_document_bench
//   /tmp/text_document_bench --sizes 10,100,1000
//
// Sizes are megabytes. --long adds a single-line file of the largest size. Generated content
// is deterministic, so runs are comparable.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr uint32_t BLOCK_SIZE = 4096;
constexpr uint32_t VIEWPORT_LINES = 60;

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// ---------------------------------------------------------------- corpus

// Source-like text: short lines, occasional long ones, deterministic for a given seed.
std::string generateCorpus(size_t bytes, bool singleLine)
{
    std::mt19937_64 rng(0xC0FFEE);
    std::string out;
    out.reserve(bytes + 256);

    static const char *fragments[] = {
        "    if (node->parent != nullptr) {",
        "        return node->parent->children[index];",
        "    }",
        "",
        "void Renderer::submit(const DrawCall &call)",
        "{",
        "    m_calls.push_back(call);",
        "}",
        "// TODO: this allocates on every frame",
        "static constexpr uint32_t MAX_ENTRIES = 4096;",
    };
    constexpr size_t fragmentCount = sizeof(fragments) / sizeof(fragments[0]);

    while (out.size() < bytes) {
        const char *fragment = fragments[rng() % fragmentCount];
        out.append(fragment);
        if (!singleLine) {
            out.push_back('\n');
        } else {
            out.push_back(' ');
        }
    }
    out.resize(bytes);
    return out;
}

// ---------------------------------------------------------------- flat

// The whole file plus one offset per line start. Line i is [offset[i], offset[i+1] - 1).
class FlatDocument {
  public:
    void build(std::string text)
    {
        m_text = std::move(text);
        m_offsets.clear();
        m_offsets.push_back(0);

        const char *base = m_text.data();
        const char *cursor = base;
        const char *end = base + m_text.size();
        while (cursor < end) {
            const char *nl = static_cast<const char *>(std::memchr(cursor, '\n', static_cast<size_t>(end - cursor)));
            if (nl == nullptr) {
                break;
            }
            m_offsets.push_back(static_cast<uint64_t>(nl - base) + 1);
            cursor = nl + 1;
        }
        m_offsets.push_back(m_text.size() + 1);
    }

    uint64_t lineCount() const { return m_offsets.size() - 1; }

    std::string_view line(uint64_t index) const
    {
        uint64_t start = m_offsets[index];
        uint64_t stop = m_offsets[index + 1] - 1;
        return std::string_view(m_text.data() + start, stop - start);
    }

    size_t indexBytes() const { return m_offsets.capacity() * sizeof(uint64_t); }
    size_t textBytes() const { return m_text.capacity(); }

    // Insert into the buffer, then shift every offset past the edit. Splitting the offset
    // array into pieces with a delta, as RAD's TXT_LineMap does, avoids the second half.
    void insert(uint64_t lineIndex, std::string_view what, double &moveMs, double &patchMs)
    {
        uint64_t at = m_offsets[lineIndex];

        Clock::time_point moveStart = Clock::now();
        m_text.insert(at, what);
        moveMs = msSince(moveStart);

        Clock::time_point patchStart = Clock::now();
        for (size_t i = lineIndex + 1; i < m_offsets.size(); i++) {
            m_offsets[i] += what.size();
        }
        patchMs = msSince(patchStart);
    }

  private:
    std::string m_text;
    std::vector<uint64_t> m_offsets;
};

// ---------------------------------------------------------------- tree

// Leaves hold line bytes at the end of the block growing backwards, and a u32 start offset
// per line at the front growing forwards. Branches hold a line count per child, so a line
// number resolves by summing counts down the descent rather than through a global table.
class TreeDocument {
  public:
    void build(const std::string &text)
    {
        m_blocks.clear();
        m_leafCount = 0;

        size_t cursor = 0;
        Leaf current;
        while (cursor <= text.size()) {
            size_t nl = text.find('\n', cursor);
            size_t stop = (nl == std::string::npos) ? text.size() : nl;
            std::string_view line(text.data() + cursor, stop - cursor);

            if (!current.fits(line.size())) {
                pushLeaf(std::move(current));
                uint32_t needed = static_cast<uint32_t>(line.size() + sizeof(uint32_t));
                current = Leaf(std::max(BLOCK_SIZE, needed));
            }
            current.append(line);

            if (nl == std::string::npos) {
                break;
            }
            cursor = nl + 1;
        }
        pushLeaf(std::move(current));
        buildBranches();
    }

    uint64_t lineCount() const { return m_totalLines; }

    std::string_view line(uint64_t index)
    {
        if (index >= m_lockedLow && index < m_lockedHigh) {
            return m_leaves[m_lockedLeaf].line(index - m_lockedLow);
        }

        uint64_t base = 0;
        uint32_t node = m_root;
        while (!m_blocks[node].isLeaf) {
            const Branch &branch = m_blocks[node].branch;
            for (size_t i = 0; i < branch.children.size(); i++) {
                if (index < base + branch.counts[i]) {
                    node = branch.children[i];
                    break;
                }
                base += branch.counts[i];
            }
        }

        uint32_t leaf = m_blocks[node].leafIndex;
        m_lockedLeaf = leaf;
        m_lockedLow = base;
        m_lockedHigh = base + m_leaves[leaf].count();
        return m_leaves[leaf].line(index - base);
    }

    // One u32 index slot per line, plus the branch nodes. Line bytes are not counted: they
    // are the file itself, which both structures hold.
    size_t indexBytes() const
    {
        size_t branchBytes = 0;
        for (const Node &node : m_blocks) {
            if (!node.isLeaf) {
                branchBytes += node.branch.children.capacity() * sizeof(uint32_t);
                branchBytes += node.branch.counts.capacity() * sizeof(uint64_t);
            }
        }
        return m_totalLines * sizeof(uint32_t) + branchBytes;
    }

    size_t textBytes() const
    {
        size_t total = 0;
        for (const Leaf &leaf : m_leaves) {
            total += leaf.blockSize;
        }
        return total;
    }

    // The edit lands inside one leaf. Counts above it are stale until a walk fixes them,
    // which is what memline defers through ml_locked_lineadd.
    void insert(uint64_t lineIndex, std::string_view what, double &moveMs, double &patchMs)
    {
        Clock::time_point moveStart = Clock::now();
        line(lineIndex);
        m_leaves[m_lockedLeaf].growLine(static_cast<uint32_t>(lineIndex - m_lockedLow), what.size());
        moveMs = msSince(moveStart);
        patchMs = 0.0;
    }

  private:
    // A line too big for one block gets a block sized to hold it, as memline does.
    struct Leaf {
        explicit Leaf(uint32_t size = BLOCK_SIZE) : bytes(size), blockSize(size), textStart(size) {}

        std::vector<char> bytes;
        std::vector<uint32_t> starts;
        uint32_t blockSize;
        uint32_t textStart;

        bool fits(size_t lineBytes) const
        {
            size_t needed = lineBytes + sizeof(uint32_t);
            size_t used = starts.size() * sizeof(uint32_t) + (blockSize - textStart);
            return used + needed <= blockSize;
        }

        void append(std::string_view text)
        {
            textStart -= static_cast<uint32_t>(text.size());
            std::memcpy(bytes.data() + textStart, text.data(), text.size());
            starts.push_back(textStart);
        }

        uint32_t count() const { return static_cast<uint32_t>(starts.size()); }

        std::string_view line(uint64_t local) const
        {
            uint32_t start = starts[local];
            uint32_t stop = (local == 0) ? blockSize : starts[local - 1];
            return std::string_view(bytes.data() + start, stop - start);
        }

        // Widening a line inside the block moves only the bytes below it.
        void growLine(uint32_t local, size_t extra)
        {
            uint32_t start = starts[local];
            std::memmove(bytes.data() + textStart - extra, bytes.data() + textStart, start - textStart);
            textStart -= static_cast<uint32_t>(extra);
            for (uint32_t i = local; i < starts.size(); i++) {
                starts[i] -= static_cast<uint32_t>(extra);
            }
        }
    };

    struct Branch {
        std::vector<uint32_t> children;
        std::vector<uint64_t> counts;
    };

    struct Node {
        bool isLeaf = false;
        uint32_t leafIndex = 0;
        Branch branch;
    };

    void pushLeaf(Leaf leaf)
    {
        if (leaf.count() == 0) {
            return;
        }
        m_totalLines += leaf.count();
        m_leaves.push_back(std::move(leaf));
        Node node;
        node.isLeaf = true;
        node.leafIndex = static_cast<uint32_t>(m_leaves.size() - 1);
        m_blocks.push_back(std::move(node));
        m_leafCount++;
    }

    // Branch fanout matches what a 4096-byte pointer block holds at memline's 24-byte entry.
    void buildBranches()
    {
        constexpr size_t FANOUT = (BLOCK_SIZE - 8) / 24;

        std::vector<uint32_t> level;
        for (uint32_t i = 0; i < m_blocks.size(); i++) {
            level.push_back(i);
        }

        while (level.size() > 1) {
            std::vector<uint32_t> next;
            for (size_t i = 0; i < level.size(); i += FANOUT) {
                Node node;
                node.isLeaf = false;
                for (size_t j = i; j < std::min(i + FANOUT, level.size()); j++) {
                    node.branch.children.push_back(level[j]);
                    node.branch.counts.push_back(countOf(level[j]));
                }
                m_blocks.push_back(std::move(node));
                next.push_back(static_cast<uint32_t>(m_blocks.size() - 1));
            }
            level = std::move(next);
        }
        m_root = level.empty() ? 0 : level[0];
    }

    uint64_t countOf(uint32_t nodeIndex) const
    {
        const Node &node = m_blocks[nodeIndex];
        if (node.isLeaf) {
            return m_leaves[node.leafIndex].count();
        }
        uint64_t total = 0;
        for (uint64_t count : node.branch.counts) {
            total += count;
        }
        return total;
    }

    std::vector<Leaf> m_leaves;
    std::vector<Node> m_blocks;
    uint32_t m_root = 0;
    uint32_t m_leafCount = 0;
    uint64_t m_totalLines = 0;

    uint32_t m_lockedLeaf = 0;
    uint64_t m_lockedLow = 1;
    uint64_t m_lockedHigh = 0;
};

// ---------------------------------------------------------------- measure

struct Result {
    double buildMs = 0.0;
    double scanMs = 0.0;
    double randomMs = 0.0;
    double editMoveMs = 0.0;
    double editPatchMs = 0.0;
    size_t indexBytes = 0;
    size_t textBytes = 0;
    uint64_t lines = 0;
    uint64_t checksum = 0;
};

template <typename Document> Result measure(const std::string &corpus, uint32_t iterations)
{
    Result result;
    Document doc;

    Clock::time_point buildStart = Clock::now();
    doc.build(corpus);
    result.buildMs = msSince(buildStart);

    result.lines = doc.lineCount();
    result.indexBytes = doc.indexBytes();
    result.textBytes = doc.textBytes();
    if (result.lines == 0) {
        return result;
    }

    std::mt19937_64 rng(0x5EED);
    std::vector<uint64_t> starts(iterations);
    for (uint64_t &start : starts) {
        uint64_t span = result.lines > VIEWPORT_LINES ? result.lines - VIEWPORT_LINES : 1;
        start = rng() % span;
    }

    // A frame's worth of lines from a random position: the render path.
    Clock::time_point scanStart = Clock::now();
    for (uint64_t start : starts) {
        for (uint64_t i = 0; i < VIEWPORT_LINES && start + i < result.lines; i++) {
            std::string_view text = doc.line(start + i);
            result.checksum += text.size();
        }
    }
    result.scanMs = msSince(scanStart);

    // One line from nowhere in particular: scroll-to-anywhere.
    Clock::time_point randomStart = Clock::now();
    for (uint64_t start : starts) {
        std::string_view text = doc.line(start);
        result.checksum += text.size();
    }
    result.randomMs = msSince(randomStart);

    double moveMs = 0.0;
    double patchMs = 0.0;
    for (uint32_t i = 0; i < 16; i++) {
        double move = 0.0;
        double patch = 0.0;
        doc.insert(result.lines / 2, "x", move, patch);
        moveMs += move;
        patchMs += patch;
    }
    result.editMoveMs = moveMs / 16.0;
    result.editPatchMs = patchMs / 16.0;
    return result;
}

void report(const char *label, const Result &result)
{
    std::printf("  %-6s %9.1f %10.2f %10.2f %10.4f %10.4f %11.1f %11.1f\n", label, result.buildMs, result.scanMs,
                result.randomMs, result.editMoveMs, result.editPatchMs, result.indexBytes / 1e6, result.textBytes / 1e6);
}

} // namespace

int main(int argc, char **argv)
{
    std::vector<size_t> sizesMb = {10, 100};
    bool longLine = false;
    uint32_t iterations = 2000;

    for (int i = 1; i < argc; i++) {
        std::string_view arg = argv[i];
        if (arg == "--sizes" && i + 1 < argc) {
            sizesMb.clear();
            std::string list = argv[++i];
            size_t pos = 0;
            while (pos < list.size()) {
                size_t comma = list.find(',', pos);
                if (comma == std::string::npos) {
                    comma = list.size();
                }
                sizesMb.push_back(std::stoul(list.substr(pos, comma - pos)));
                pos = comma + 1;
            }
        } else if (arg == "--long") {
            longLine = true;
        } else if (arg == "--iterations" && i + 1 < argc) {
            iterations = static_cast<uint32_t>(std::stoul(argv[++i]));
        }
    }

    std::printf("viewport %u lines, %u sampled positions\n", VIEWPORT_LINES, iterations);

    for (size_t mb : sizesMb) {
        std::string corpus = generateCorpus(mb * 1000 * 1000, false);
        Result flat = measure<FlatDocument>(corpus, iterations);
        Result tree = measure<TreeDocument>(corpus, iterations);

        std::printf("\n=== %zu MB, %llu lines ===\n", mb, static_cast<unsigned long long>(flat.lines));
        std::printf("  %-6s %9s %10s %10s %10s %10s %11s %11s\n", "", "build ms", "scan ms", "rand ms", "editMv ms",
                    "editPa ms", "index MB", "text MB");
        report("flat", flat);
        report("tree", tree);
    }

    if (longLine && !sizesMb.empty()) {
        size_t mb = sizesMb.back();
        std::string corpus = generateCorpus(mb * 1000 * 1000, true);
        Result flat = measure<FlatDocument>(corpus, iterations);
        Result tree = measure<TreeDocument>(corpus, iterations);

        std::printf("\n=== %zu MB as one line ===\n", mb);
        std::printf("  %-6s %9s %10s %10s %10s %10s %11s %11s\n", "", "build ms", "scan ms", "rand ms", "editMv ms",
                    "editPa ms", "index MB", "text MB");
        report("flat", flat);
        report("tree", tree);
    }

    return 0;
}

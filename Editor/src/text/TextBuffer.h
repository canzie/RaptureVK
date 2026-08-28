#ifndef RAPTURE__TEXT_BUFFER_H
#define RAPTURE__TEXT_BUFFER_H

#include <modules/text_source.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

/**
 * @brief Editable text held as a tree of blocks, addressed by line.
 *
 * Text blocks hold the line bytes; index blocks above them hold a line count per child, so a
 * line number resolves by summing counts down the descent and no document-wide line table
 * exists. An edit rewrites one text block and adds its delta to the counts on the path to the
 * root, so the cost of typing does not depend on how large the text is.
 *
 * A text block is capped at BLOCK_BYTES so the memmove an edit performs stays bounded. A
 * single line longer than that gets a block sized to hold it.
 */
/**
 * @brief The terminator a document's lines are separated by in its file.
 */
enum class LineEnding {
    LF,
    CRLF
};

class TextBuffer : public Amethyst::TextSourceBase {
  public:
    static constexpr uint32_t BLOCK_BYTES = 4096;

    TextBuffer();
    explicit TextBuffer(std::string_view text);

    uint64_t lineCount() const override { return m_lineCount; }
    std::string_view line(uint64_t index) const override;
    void replace(Amethyst::TextRange range, std::string_view with) override;
    uint64_t revision() const override { return m_revision; }

    /**
     * @brief Discard the contents and rebuild from text, splitting it on line terminators.
     *
     * Lines are held without terminators whatever the file used, and the kind it used is
     * recorded so writing it back does not rewrite every line.
     *
     * @param text Replacement contents
     */
    void load(std::string_view text);

    /**
     * @brief The whole contents, lines joined with the recorded terminator.
     */
    std::string text() const;

    LineEnding lineEnding() const { return m_lineEnding; }
    void setLineEnding(LineEnding ending) { m_lineEnding = ending; }

    /**
     * @brief Byte length of a line, without its terminator.
     * @param index Line to measure; out of range yields 0
     */
    uint64_t lineLength(uint64_t index) const;

  private:
    /**
     * @brief Whole lines back to back, with an offset table naming where each starts.
     *
     * Lines fill the block from the end backwards while the offset table grows from the
     * front, so widening a line moves only the lines below it within this one block.
     */
    struct TextBlock {
        std::vector<char> bytes;
        std::vector<uint32_t> lineStarts;
        uint32_t parent = INVALID;
        uint32_t capacity = BLOCK_BYTES;
        uint32_t textStart = BLOCK_BYTES;

        uint32_t lineCount() const { return static_cast<uint32_t>(lineStarts.size()); }
        uint32_t usedBytes() const { return capacity - textStart; }
        bool fits(size_t lineBytes) const;
        std::string_view line(uint32_t local) const;
    };

    /**
     * @brief Child blocks, and how many lines each of their subtrees spans.
     */
    struct IndexBlock {
        std::vector<uint32_t> children;
        std::vector<uint64_t> lineCounts;
        uint32_t parent = INVALID;
    };

    static constexpr uint32_t INVALID = UINT32_MAX;

    // A child id names a block in one of the two arrays; the flag says which.
    static constexpr uint32_t TEXT_BLOCK_FLAG = 0x80000000u;

    static bool isTextBlock(uint32_t id) { return (id & TEXT_BLOCK_FLAG) != 0; }
    static uint32_t blockIndex(uint32_t id) { return id & ~TEXT_BLOCK_FLAG; }

    /**
     * @brief Find the text block holding a line, caching it so neighbouring lines cost nothing.
     * @param index Line to locate
     * @return Text block index, or INVALID if the line is out of range
     */
    uint32_t textBlockForLine(uint64_t index) const;

    /**
     * @brief Build the index blocks over the current text blocks, bottom up.
     */
    void buildIndex();

    /**
     * @brief Lines spanned by a block and everything beneath it.
     * @param id Child id of either kind
     */
    uint64_t subtreeLineCount(uint32_t id) const;

    /**
     * @brief Record which index block a child now hangs from.
     * @param id Child id of either kind
     * @param parent Index block the child belongs to
     */
    void setParent(uint32_t id, uint32_t parent);

    /**
     * @brief Distribute lines into as few text blocks as the capacity allows.
     * @param lines Lines to pack, in order
     * @return The blocks holding them, in the same order
     */
    static std::vector<TextBlock> packLines(const std::vector<std::string> &lines);

    /**
     * @brief Append a line to the last text block, starting a new one when it will not fit.
     * @param text Line contents, without a terminator
     */
    void appendLine(std::string_view text);

    /**
     * @brief Replace a run of lines with new ones, rewriting the blocks they occupy.
     * @param first First line to remove
     * @param count How many lines to remove
     * @param replacement Lines to put in their place
     */
    void spliceLines(uint64_t first, uint64_t count, const std::vector<std::string> &replacement);

    /**
     * @brief Add a delta to the line count of every index block above a text block.
     * @param textBlock Block whose line count changed
     * @param delta Lines gained, or lost when negative
     */
    void propagateLineCount(uint32_t textBlock, int64_t delta);

    std::vector<TextBlock> m_textBlocks;
    std::vector<IndexBlock> m_indexBlocks;
    uint32_t m_root = INVALID;
    uint64_t m_lineCount = 0;
    uint64_t m_revision = 0;
    LineEnding m_lineEnding = LineEnding::LF;

    // A descent caches the block it landed in and the lines that block covers, so reading a
    // run of consecutive lines costs one descent rather than one per line.
    mutable uint32_t m_cachedBlock = INVALID;
    mutable uint64_t m_cachedFirstLine = 0;
    mutable uint64_t m_cachedEndLine = 0;
};

#endif // RAPTURE__TEXT_BUFFER_H

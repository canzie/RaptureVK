#ifndef RAPTURE__SERIAL_DOCUMENT_H
#define RAPTURE__SERIAL_DOCUMENT_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Rapture {

/**
 * @brief Non-owning write cursor into a SerialDocument being built.
 *
 * A cheap value type (a document pointer + a node pointer) that attaches every
 * operation into the tree immediately. Invalid cursors are safe no-ops.
 */
class ReadNode;

class WriteNode {
  public:
    WriteNode() = default;

    /**
     * @brief Whether this cursor points at a live node.
     * @return True if the cursor can be written to.
     */
    bool valid() const;

    /**
     * @brief Create an empty child object under a key and return a cursor to it.
     * @param key Member key on this (object) node.
     * @return Cursor to the new child object, invalid if this is not an object.
     */
    WriteNode addObject(std::string_view key);

    /**
     * @brief Create an empty child array under a key and return a cursor to it.
     * @param key Member key on this (object) node.
     * @return Cursor to the new child array, invalid if this is not an object.
     */
    WriteNode addArray(std::string_view key);

    /**
     * @brief Append an empty object element to this array node.
     * @return Cursor to the new element, invalid if this is not an array.
     */
    WriteNode appendObject();

    /**
     * @brief Append an empty array element to this array node.
     * @return Cursor to the new element, invalid if this is not an array.
     */
    WriteNode appendArray();

    /**
     * @brief Deep copy a parsed subtree in as a member of this object node.
     * @param key Member key on this (object) node.
     * @param source Cursor to the subtree to copy, which may belong to another document.
     * @return Cursor to the copy, invalid if the source is unreadable or this is not an object.
     */
    WriteNode addCopy(std::string_view key, ReadNode source);

    /**
     * @brief Set an unsigned integer member on this object node.
     * @param key Member key.
     * @param v Value stored as a native uint64 (round-trips exactly).
     */
    void set(std::string_view key, uint64_t v);

    /**
     * @brief Set a signed integer member on this object node.
     * @param key Member key.
     * @param v Value.
     */
    void set(std::string_view key, int64_t v);

    /**
     * @brief Set a single precision member on this object node.
     *
     * Written as the shortest text that reads back as the same float, so a value like 0.8f
     * stays "0.8" instead of the double it promotes to.
     *
     * @param key Member key.
     * @param v Value, must be finite.
     */
    void set(std::string_view key, float v);

    /**
     * @brief Set a floating point member on this object node.
     * @param key Member key.
     * @param v Value.
     */
    void set(std::string_view key, double v);

    /**
     * @brief Set a boolean member on this object node.
     * @param key Member key.
     * @param v Value.
     */
    void set(std::string_view key, bool v);

    /**
     * @brief Set a string member on this object node (the string is copied).
     * @param key Member key.
     * @param v Value.
     */
    void set(std::string_view key, std::string_view v);

    /**
     * @brief Set a string member from a C string (the string is copied).
     * @param key Member key.
     * @param v Value, ignored if null.
     */
    void set(std::string_view key, const char *v)
    {
        if (v != nullptr) {
            set(key, std::string_view(v));
        }
    }

    /**
     * @brief Append an unsigned integer element to this array node.
     * @param v Value stored as a native uint64 (round-trips exactly).
     */
    void append(uint64_t v);

    /**
     * @brief Append a signed integer element to this array node.
     * @param v Value.
     */
    void append(int64_t v);

    /**
     * @brief Append a single precision element to this array node.
     *
     * Written as the shortest text that reads back as the same float, so a value like 0.8f
     * stays "0.8" instead of the double it promotes to.
     *
     * @param v Value, must be finite.
     */
    void append(float v);

    /**
     * @brief Append a floating point element to this array node.
     * @param v Value.
     */
    void append(double v);

    /**
     * @brief Append a boolean element to this array node.
     * @param v Value.
     */
    void append(bool v);

    /**
     * @brief Append a string element to this array node (the string is copied).
     * @param v Value.
     */
    void append(std::string_view v);

    /**
     * @brief Append a string element from a C string (the string is copied).
     * @param v Value, ignored if null.
     */
    void append(const char *v)
    {
        if (v != nullptr) {
            append(std::string_view(v));
        }
    }

  private:
    friend class SerialDocument;
    WriteNode(void *doc, void *node);

    void *m_doc = nullptr;
    void *m_node = nullptr;
};

/**
 * @brief Non-owning read cursor into a parsed SerialDocument.
 *
 * A cheap value type. Typed gets always take a fallback and never fail: a
 * missing key or wrong type returns the fallback.
 */
class ReadNode {
  public:
    ReadNode() = default;

    /**
     * @brief Whether this cursor points at a real node.
     * @return True if the cursor is usable.
     */
    bool valid() const;

    /**
     * @brief Resolve a member of this object node by key.
     * @param key Member key.
     * @return Cursor to the member, invalid if missing or this is not an object.
     */
    ReadNode child(std::string_view key) const;

    /**
     * @brief Read this node as an unsigned integer.
     * @param fallback Returned when the node is not an integer.
     * @return The stored uint64 or the fallback.
     */
    uint64_t asU64(uint64_t fallback = 0) const;

    /**
     * @brief Read this node as a signed integer.
     * @param fallback Returned when the node is not an integer.
     * @return The stored int64 or the fallback.
     */
    int64_t asI64(int64_t fallback = 0) const;

    /**
     * @brief Read this node as a floating point number.
     * @param fallback Returned when the node is not a number.
     * @return The stored number or the fallback.
     */
    double asF64(double fallback = 0.0) const;

    /**
     * @brief Read this node as a boolean.
     * @param fallback Returned when the node is not a boolean.
     * @return The stored bool or the fallback.
     */
    bool asBool(bool fallback = false) const;

    /**
     * @brief Read this node as a string.
     * @param fallback Returned when the node is not a string.
     * @return A view into the parsed document, valid while the document lives.
     */
    std::string_view asString(std::string_view fallback = {}) const;

    /**
     * @brief Element count of an array, or member count of an object.
     * @return The count, 0 if this is neither.
     */
    size_t size() const;

    /**
     * @brief Resolve an element of this array node by index.
     * @param i Element index.
     * @return Cursor to the element, invalid if out of range or not an array.
     */
    ReadNode at(size_t i) const;

  private:
    friend class SerialDocument;
    friend class WriteNode;
    explicit ReadNode(void *val);

    void *m_val = nullptr;
};

/**
 * @brief Owns a backing document tree; the swappable serialization backend.
 *
 * Default construction opens a write-mode document; parse() opens a read-mode
 * document. Non-copyable, movable. No backend types appear in this header so the
 * format can be swapped by reimplementing the .cpp.
 */
class SerialDocument {
  public:
    SerialDocument();
    ~SerialDocument();

    SerialDocument(SerialDocument &&other) noexcept;
    SerialDocument &operator=(SerialDocument &&other) noexcept;
    SerialDocument(const SerialDocument &) = delete;
    SerialDocument &operator=(const SerialDocument &) = delete;

    /**
     * @brief Open a read-mode document from serialized text.
     * @param text Serialized bytes to parse.
     * @return A read-mode document, invalid if parsing failed.
     */
    static SerialDocument parse(std::string_view text);

    /**
     * @brief Open a read-mode document holding a copy of a parsed subtree.
     * @param source Cursor to the subtree to copy, which the result no longer depends on.
     * @return A read-mode document rooted at the copy, unreadable if the source is unreadable.
     */
    static SerialDocument copyOf(ReadNode source);

    /**
     * @brief Turn a write-mode document into a read-mode one holding what was written.
     * @return True if the document can be read from.
     */
    bool freeze();

    /**
     * @brief Whether this document can be read from, so whether rootView returns a live cursor.
     */
    bool isReadable() const { return m_doc != nullptr; }

    /**
     * @brief Serialize the tree to text.
     * @param pretty Whether to indent and space the output instead of minifying it.
     * @return The serialized bytes, empty on failure.
     */
    std::string toText(bool pretty = false) const;

    /**
     * @brief Cursor to the root of a write-mode document.
     * @return The root write cursor, invalid for a read-mode document.
     */
    WriteNode root();

    /**
     * @brief Cursor to the root of a read-mode document.
     * @return The root read cursor, invalid for a write-mode document.
     */
    ReadNode rootView() const;

  private:
    void *m_mutDoc = nullptr;
    void *m_doc = nullptr;
};

} // namespace Rapture

#endif // RAPTURE__SERIAL_DOCUMENT_H

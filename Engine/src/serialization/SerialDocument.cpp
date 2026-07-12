#include "SerialDocument.h"

#include <cstdlib>

#include <yyjson.h>

#include "logging/Log.h"

namespace Rapture {

static yyjson_mut_doc *s_asMutDoc(void *doc)
{
    return static_cast<yyjson_mut_doc *>(doc);
}

static yyjson_mut_val *s_asMutVal(void *node)
{
    return static_cast<yyjson_mut_val *>(node);
}

static yyjson_val *s_asVal(void *val)
{
    return static_cast<yyjson_val *>(val);
}

WriteNode::WriteNode(void *doc, void *node) : m_doc(doc), m_node(node) {}

bool WriteNode::valid() const
{
    return (m_doc != nullptr) && (m_node != nullptr);
}

WriteNode WriteNode::addObject(std::string_view key)
{
    if (!valid()) {
        return WriteNode();
    }
    yyjson_mut_doc *doc = s_asMutDoc(m_doc);
    yyjson_mut_val *keyVal = yyjson_mut_strncpy(doc, key.data(), key.size());
    yyjson_mut_val *child = yyjson_mut_obj(doc);
    if (!yyjson_mut_obj_add(s_asMutVal(m_node), keyVal, child)) {
        return WriteNode();
    }
    return WriteNode(m_doc, child);
}

WriteNode WriteNode::addArray(std::string_view key)
{
    if (!valid()) {
        return WriteNode();
    }
    yyjson_mut_doc *doc = s_asMutDoc(m_doc);
    yyjson_mut_val *keyVal = yyjson_mut_strncpy(doc, key.data(), key.size());
    yyjson_mut_val *child = yyjson_mut_arr(doc);
    if (!yyjson_mut_obj_add(s_asMutVal(m_node), keyVal, child)) {
        return WriteNode();
    }
    return WriteNode(m_doc, child);
}

WriteNode WriteNode::appendObject()
{
    if (!valid()) {
        return WriteNode();
    }
    yyjson_mut_val *child = yyjson_mut_obj(s_asMutDoc(m_doc));
    if (!yyjson_mut_arr_append(s_asMutVal(m_node), child)) {
        return WriteNode();
    }
    return WriteNode(m_doc, child);
}

WriteNode WriteNode::appendArray()
{
    if (!valid()) {
        return WriteNode();
    }
    yyjson_mut_val *child = yyjson_mut_arr(s_asMutDoc(m_doc));
    if (!yyjson_mut_arr_append(s_asMutVal(m_node), child)) {
        return WriteNode();
    }
    return WriteNode(m_doc, child);
}

void WriteNode::set(std::string_view key, uint64_t v)
{
    if (!valid()) {
        return;
    }
    yyjson_mut_doc *doc = s_asMutDoc(m_doc);
    yyjson_mut_val *keyVal = yyjson_mut_strncpy(doc, key.data(), key.size());
    yyjson_mut_obj_add(s_asMutVal(m_node), keyVal, yyjson_mut_uint(doc, v));
}

void WriteNode::set(std::string_view key, int64_t v)
{
    if (!valid()) {
        return;
    }
    yyjson_mut_doc *doc = s_asMutDoc(m_doc);
    yyjson_mut_val *keyVal = yyjson_mut_strncpy(doc, key.data(), key.size());
    yyjson_mut_obj_add(s_asMutVal(m_node), keyVal, yyjson_mut_sint(doc, v));
}

void WriteNode::set(std::string_view key, double v)
{
    if (!valid()) {
        return;
    }
    yyjson_mut_doc *doc = s_asMutDoc(m_doc);
    yyjson_mut_val *keyVal = yyjson_mut_strncpy(doc, key.data(), key.size());
    yyjson_mut_obj_add(s_asMutVal(m_node), keyVal, yyjson_mut_real(doc, v));
}

void WriteNode::set(std::string_view key, bool v)
{
    if (!valid()) {
        return;
    }
    yyjson_mut_doc *doc = s_asMutDoc(m_doc);
    yyjson_mut_val *keyVal = yyjson_mut_strncpy(doc, key.data(), key.size());
    yyjson_mut_obj_add(s_asMutVal(m_node), keyVal, yyjson_mut_bool(doc, v));
}

void WriteNode::set(std::string_view key, std::string_view v)
{
    if (!valid()) {
        return;
    }
    yyjson_mut_doc *doc = s_asMutDoc(m_doc);
    yyjson_mut_val *keyVal = yyjson_mut_strncpy(doc, key.data(), key.size());
    yyjson_mut_val *strVal = yyjson_mut_strncpy(doc, v.data(), v.size());
    yyjson_mut_obj_add(s_asMutVal(m_node), keyVal, strVal);
}

void WriteNode::append(uint64_t v)
{
    if (!valid()) {
        return;
    }
    yyjson_mut_arr_append(s_asMutVal(m_node), yyjson_mut_uint(s_asMutDoc(m_doc), v));
}

void WriteNode::append(int64_t v)
{
    if (!valid()) {
        return;
    }
    yyjson_mut_arr_append(s_asMutVal(m_node), yyjson_mut_sint(s_asMutDoc(m_doc), v));
}

void WriteNode::append(double v)
{
    if (!valid()) {
        return;
    }
    yyjson_mut_arr_append(s_asMutVal(m_node), yyjson_mut_real(s_asMutDoc(m_doc), v));
}

void WriteNode::append(bool v)
{
    if (!valid()) {
        return;
    }
    yyjson_mut_arr_append(s_asMutVal(m_node), yyjson_mut_bool(s_asMutDoc(m_doc), v));
}

void WriteNode::append(std::string_view v)
{
    if (!valid()) {
        return;
    }
    yyjson_mut_doc *doc = s_asMutDoc(m_doc);
    yyjson_mut_val *strVal = yyjson_mut_strncpy(doc, v.data(), v.size());
    yyjson_mut_arr_append(s_asMutVal(m_node), strVal);
}

ReadNode::ReadNode(void *val) : m_val(val) {}

bool ReadNode::valid() const
{
    return m_val != nullptr;
}

ReadNode ReadNode::child(std::string_view key) const
{
    if (m_val == nullptr) {
        return ReadNode(nullptr);
    }
    return ReadNode(yyjson_obj_getn(s_asVal(m_val), key.data(), key.size()));
}

uint64_t ReadNode::asU64(uint64_t fallback) const
{
    yyjson_val *val = s_asVal(m_val);
    if (yyjson_is_int(val)) {
        return yyjson_get_uint(val);
    }
    return fallback;
}

int64_t ReadNode::asI64(int64_t fallback) const
{
    yyjson_val *val = s_asVal(m_val);
    if (yyjson_is_int(val)) {
        return yyjson_get_sint(val);
    }
    return fallback;
}

double ReadNode::asF64(double fallback) const
{
    yyjson_val *val = s_asVal(m_val);
    if (yyjson_is_num(val)) {
        return yyjson_get_num(val);
    }
    return fallback;
}

bool ReadNode::asBool(bool fallback) const
{
    yyjson_val *val = s_asVal(m_val);
    if (yyjson_is_bool(val)) {
        return yyjson_get_bool(val);
    }
    return fallback;
}

std::string_view ReadNode::asString(std::string_view fallback) const
{
    yyjson_val *val = s_asVal(m_val);
    if (yyjson_is_str(val)) {
        return std::string_view(yyjson_get_str(val), yyjson_get_len(val));
    }
    return fallback;
}

size_t ReadNode::size() const
{
    yyjson_val *val = s_asVal(m_val);
    if (yyjson_is_arr(val)) {
        return yyjson_arr_size(val);
    }
    if (yyjson_is_obj(val)) {
        return yyjson_obj_size(val);
    }
    return 0;
}

ReadNode ReadNode::at(size_t i) const
{
    if (m_val == nullptr) {
        return ReadNode(nullptr);
    }
    return ReadNode(yyjson_arr_get(s_asVal(m_val), i));
}

SerialDocument::SerialDocument()
{
    m_mutDoc = yyjson_mut_doc_new(nullptr);
    if (m_mutDoc == nullptr) {
        RP_CORE_ERROR("failed to allocate write document");
        return;
    }
    yyjson_mut_doc *doc = s_asMutDoc(m_mutDoc);
    yyjson_mut_doc_set_root(doc, yyjson_mut_obj(doc));
}

SerialDocument::~SerialDocument()
{
    if (m_mutDoc != nullptr) {
        yyjson_mut_doc_free(s_asMutDoc(m_mutDoc));
    }
    if (m_doc != nullptr) {
        yyjson_doc_free(static_cast<yyjson_doc *>(m_doc));
    }
}

SerialDocument::SerialDocument(SerialDocument &&other) noexcept : m_mutDoc(other.m_mutDoc), m_doc(other.m_doc)
{
    other.m_mutDoc = nullptr;
    other.m_doc = nullptr;
}

SerialDocument &SerialDocument::operator=(SerialDocument &&other) noexcept
{
    if (this != &other) {
        if (m_mutDoc != nullptr) {
            yyjson_mut_doc_free(s_asMutDoc(m_mutDoc));
        }
        if (m_doc != nullptr) {
            yyjson_doc_free(static_cast<yyjson_doc *>(m_doc));
        }
        m_mutDoc = other.m_mutDoc;
        m_doc = other.m_doc;
        other.m_mutDoc = nullptr;
        other.m_doc = nullptr;
    }
    return *this;
}

SerialDocument SerialDocument::parse(std::string_view text)
{
    SerialDocument out;
    if (out.m_mutDoc != nullptr) {
        yyjson_mut_doc_free(s_asMutDoc(out.m_mutDoc));
        out.m_mutDoc = nullptr;
    }
    out.m_doc = yyjson_read(text.data(), text.size(), YYJSON_READ_NOFLAG);
    if (out.m_doc == nullptr) {
        RP_CORE_ERROR("failed to parse document text");
    }
    return out;
}

std::string SerialDocument::toText() const
{
    char *text = nullptr;
    size_t len = 0;
    if (m_mutDoc != nullptr) {
        text = yyjson_mut_write(s_asMutDoc(m_mutDoc), YYJSON_WRITE_PRETTY, &len);
    } else if (m_doc != nullptr) {
        text = yyjson_write(static_cast<yyjson_doc *>(m_doc), YYJSON_WRITE_PRETTY, &len);
    }
    if (text == nullptr) {
        RP_CORE_ERROR("failed to serialize document");
        return {};
    }
    std::string result(text, len);
    free(text);
    return result;
}

WriteNode SerialDocument::root()
{
    if (m_mutDoc == nullptr) {
        RP_CORE_ERROR("called on a read-mode document");
        return WriteNode();
    }
    yyjson_mut_doc *doc = s_asMutDoc(m_mutDoc);
    return WriteNode(m_mutDoc, yyjson_mut_doc_get_root(doc));
}

ReadNode SerialDocument::rootView() const
{
    if (m_doc == nullptr) {
        RP_CORE_ERROR("called on a write-mode document");
        return ReadNode(nullptr);
    }
    return ReadNode(yyjson_doc_get_root(static_cast<yyjson_doc *>(m_doc)));
}

} // namespace Rapture

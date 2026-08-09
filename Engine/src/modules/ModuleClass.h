#ifndef RAPTURE__MODULE_CLASS_H
#define RAPTURE__MODULE_CLASS_H

#include "serialization/SerialDocument.h"
#include "utils/TypeInfo.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace Rapture {

class Instance;

/**
 * @brief Base of every authored object that lives in the asset database rather than in a scene.
 *
 * A module owns no entity and holds no place in a scene tree. Its identity and its name are the
 * asset's, so its document holds only the fields its own class declares.
 */
class ModuleClass {
  public:
    virtual ~ModuleClass() = default;

    static const TypeInfo &staticType();

    /**
     * @brief The type of the object this actually is, rather than of the class it is held as
     */
    virtual const TypeInfo &type() const = 0;

    /**
     * @brief Whether this module is a T, or derives from one
     */
    template <typename T>
    bool isA() const
    {
        return type().isA(T::staticType());
    }

    /**
     * @brief Casts to T if this module is one
     * @return The cast pointer, or nullptr if the cast is not legal
     */
    template <typename T>
    T *as()
    {
        return isA<T>() ? static_cast<T *>(this) : nullptr;
    }

    template <typename T>
    const T *as() const
    {
        return isA<T>() ? static_cast<const T *>(this) : nullptr;
    }

    /**
     * @brief Writes this module's class and the fields its class declares
     * @param node Cursor to write this module's object into
     */
    virtual void serialize(WriteNode node) const;

    /**
     * @brief Reads the fields this module's class declares
     * @param node Cursor to this module's object
     */
    virtual void deserialize(ReadNode node);

    /**
     * @brief Reads the class a document names, before there is a module to read into
     * @param node Cursor to the module's object
     * @return The class name, empty if the document names none
     */
    static std::string_view readClassName(ReadNode node);

    /**
     * @brief Creates the module a document names and reads its fields
     * @param node Cursor to the module's object
     * @return The module, or nullptr if no class is registered under the document's name
     */
    static std::unique_ptr<ModuleClass> load(ReadNode node);

    /**
     * @brief Serializes this module into a self-contained blob
     *
     * Named apart from serialize so that overriding the document pair does not hide it.
     *
     * @return The serialized bytes, empty if the document could not be written
     */
    std::vector<uint8_t> toBlob() const;

    /**
     * @brief Rebuilds a module from a blob produced by toBlob
     * @param blob The serialized bytes
     * @return The module, or nullptr if the blob does not name a registered class
     */
    static std::unique_ptr<ModuleClass> fromBlob(std::span<const uint8_t> blob);
};

} // namespace Rapture

#endif // RAPTURE__MODULE_CLASS_H

#ifndef RAPTURE__TYPEINFO_H
#define RAPTURE__TYPEINFO_H

#include <cstdint>
#include <string_view>

namespace Rapture {

static constexpr uint8_t MAX_TYPE_DEPTH = 8;

/**
 * @brief Runtime type of a class, holding its ancestry as a chain indexed by depth.
 *
 * Single inheritance makes an ancestry a straight line, so a subtype test is one depth compare and
 * one pointer compare regardless of how many classes exist.
 */
struct TypeInfo {
    /**
     * @brief Builds a type by extending its base's ancestry chain with itself
     * @param name Class name, used by serialization and the outliner
     * @param base The base class type, or nullptr for the root class
     */
    TypeInfo(std::string_view name, const TypeInfo *base);

    std::string_view name;
    const TypeInfo *base;
    uint8_t depth;
    const TypeInfo *chain[MAX_TYPE_DEPTH];
};

} // namespace Rapture

#endif // RAPTURE__TYPEINFO_H

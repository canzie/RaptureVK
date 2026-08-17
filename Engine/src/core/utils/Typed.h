#ifndef RAPTURE__TYPED_H
#define RAPTURE__TYPED_H

#include "core/utils/TypeInfo.h"

namespace Rapture {

/**
 * @brief Base of any class hierarchy that wants runtime subtype tests without RTTI
 *
 * Carries no type of its own, so each hierarchy under it starts its own ancestry chain.
 */
class Typed {
  public:
    virtual ~Typed() = default;

    /**
     * @brief The type of the object this actually is, rather than of the class it is held as
     */
    virtual const TypeInfo &type() const = 0;

    /**
     * @brief Whether this object is a T, or derives from one
     */
    template <typename T>
    bool isA() const
    {
        return type().isA(T::staticType());
    }

    /**
     * @brief Casts to T if this object is one
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
};

} // namespace Rapture

#endif // RAPTURE__TYPED_H

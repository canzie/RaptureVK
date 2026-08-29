#ifndef RAPTURE__REF_H
#define RAPTURE__REF_H

#include "core/utils/RefCounted.h"

namespace Rapture {

/**
 * @brief Holds a use of a RefCounted for as long as it exists.
 *
 * The object has an owner elsewhere, so this says the object is being used rather than that it is
 * being kept alive.
 */
template <typename T>
class Ref {
  public:
    Ref() = default;

    explicit Ref(T *object) noexcept : m_object(object)
    {
        if (m_object != nullptr) {
            m_object->addRef();
        }
    }

    Ref(const Ref &other) noexcept : Ref(other.m_object) {}

    Ref(Ref &&other) noexcept : m_object(other.m_object) { other.m_object = nullptr; }

    ~Ref() noexcept { release(); }

    Ref &operator=(const Ref &other) noexcept
    {
        if (this == &other) {
            return *this;
        }

        release();
        m_object = other.m_object;
        if (m_object != nullptr) {
            m_object->addRef();
        }
        return *this;
    }

    Ref &operator=(Ref &&other) noexcept
    {
        if (this == &other) {
            return *this;
        }

        release();
        m_object = other.m_object;
        other.m_object = nullptr;
        return *this;
    }

    T *get() const noexcept { return m_object; }
    T &operator*() const noexcept { return *m_object; }

    /**
     * @brief Reaches the object, or what it stands for where it declares an arrow of its own
     * @return The target, or nullptr where this holds nothing
     */
    auto operator->() const noexcept
    {
        if constexpr (requires(T *object) { object->operator->(); }) {
            using Target = decltype(m_object->operator->());
            return m_object != nullptr ? m_object->operator->() : Target{};
        } else {
            return m_object;
        }
    }

    explicit operator bool() const noexcept { return m_object != nullptr; }
    bool operator==(const Ref &other) const noexcept { return m_object == other.m_object; }

    /**
     * @brief Narrows to a U, taking a use of its own
     * @return A use of the object as a U, empty if it is not one
     */
    template <typename U>
    Ref<U> as() const noexcept
    {
        return Ref<U>(m_object != nullptr ? m_object->template as<U>() : nullptr);
    }

  private:
    void release() noexcept
    {
        if (m_object != nullptr && m_object->releaseRef()) {
            m_object->onLastUseReleased();
        }
        m_object = nullptr;
    }

    T *m_object = nullptr;
};

} // namespace Rapture

#endif // RAPTURE__REF_H

#ifndef RAPTURE__INPLACE_FUNCTION_H
#define RAPTURE__INPLACE_FUNCTION_H

#include <cstddef>
#include <type_traits>
#include <utility>

namespace Rapture {

template <typename Sig, size_t Size> class InplaceFunction;

template <typename R, typename... Args, size_t Size> class InplaceFunction<R(Args...), Size> {
  public:
    InplaceFunction() = default;

    template <typename F> InplaceFunction(F &&f)
    {
        static_assert(sizeof(F) <= Size, "Callable too large");
        static_assert(std::is_nothrow_move_constructible_v<F>);

        new (&storage) F(std::forward<F>(f));

        invoke = [](void *s, Args &&...args) -> R { return (*reinterpret_cast<F *>(s))(std::forward<Args>(args)...); };

        destroy = [](void *s) { reinterpret_cast<F *>(s)->~F(); };

        move = [](void *src, void *dst) {
            new (dst) F(std::move(*reinterpret_cast<F *>(src)));
            reinterpret_cast<F *>(src)->~F();
        };

        copy = [](const void *src, void *dst) { new (dst) F(*reinterpret_cast<const F *>(src)); };
    }

    template <typename F> InplaceFunction(const InplaceFunction &other)
    {
        static_assert(sizeof(F) <= Size, "Callable too large");
        static_assert(std::is_copy_constructible_v<F>, "Callable must be copyable");

        new (&storage) F(*reinterpret_cast<const F *>(&other.storage));

        invoke = other.invoke;
        destroy = other.destroy;
        move = other.move;
        copy = [](const void *src, void *dst) { new (dst) F(*reinterpret_cast<const F *>(src)); };
    }

    InplaceFunction(const InplaceFunction &other)
    {
        if (other.invoke) {
            other.copy(&other.storage, &storage);
            invoke = other.invoke;
            destroy = other.destroy;
            move = other.move;
            copy = other.copy;
        }
    }

    InplaceFunction(InplaceFunction &&other) noexcept
    {
        if (other.invoke) {
            other.move(&other.storage, &storage);
            invoke = other.invoke;
            destroy = other.destroy;
            move = other.move;
            other.invoke = nullptr;
        }
    }

    InplaceFunction &operator=(InplaceFunction &&other) noexcept
    {
        if (this != &other) {
            reset();
            if (other.invoke) {
                other.move(&other.storage, &storage);
                invoke = other.invoke;
                destroy = other.destroy;
                move = other.move;
                other.invoke = nullptr;
            }
        }
        return *this;
    }

    ~InplaceFunction() { reset(); }

    R operator()(Args... args) { return invoke(&storage, std::forward<Args>(args)...); }

    explicit operator bool() const { return invoke != nullptr; }

  private:
    void reset()
    {
        if (invoke) {
            destroy(&storage);
            invoke = nullptr;
            destroy = nullptr;
            move = nullptr;
            copy = nullptr;
        }
    }

    using Storage = std::aligned_storage_t<Size, alignof(std::max_align_t)>;
    using InvokeFn = R (*)(void *, Args &&...);
    using DestroyFn = void (*)(void *);
    using MoveFn = void (*)(void *, void *);
    using CopyFn = void (*)(const void *src, void *dst);

    Storage storage;
    InvokeFn invoke = nullptr;
    DestroyFn destroy = nullptr;
    MoveFn move = nullptr;
    CopyFn copy = nullptr;
};

} // namespace Rapture

#endif // RAPTURE__INPLACE_FUNCTION_H

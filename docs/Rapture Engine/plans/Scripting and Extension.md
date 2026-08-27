# Scripting and Extension

**Related: [[Scene Object Model]], [[Scene Component]], [[Scene Object]], [[Animation System]], [[Editor Component Fields]], [[Project Serialization]], [[AssetManager]]**

An exploration of where user game code could live, written up 2026-08-24 after the [[Animation System]] state machine raised the question of what gameplay logic attaches to. Almost nothing here is settled — it is a record of what was discussed and what the tradeoffs looked like, so the ground does not have to be re-covered next time.

What did come out of it: **Lua first**, **extending `TypeInfo` into real reflection**, **user C++ as a loadable addition** rather than something compiled into the engine, and **scripts scoped to a scene object** rather than free-roaming programs.

The constraint everything else hangs off: the engine ships as a binary, so user code cannot be compiled into it.

---

## 1. Questions this was chasing

- Where does gameplay logic live, given the engine ships compiled.
- Lua or AngelScript, and what a binding actually is.
- What "user C++" means when they cannot rebuild the engine, and whether that forces a C ABI.
- What the extension surface is — which hooks the engine calls, and what user code can call back into.
- Whether a script is scoped to one object or free to reach anywhere.

---

## 2. What exists today

- `Engine` is a `STATIC` library and `Editor` is an executable linking it (`Engine/CMakeLists.txt:14`, `Editor/CMakeLists.txt:14`), so everything currently lands in one binary.
- `target_include_directories` exposes `Engine/src` as `PUBLIC` wholesale (`Engine/CMakeLists.txt:40`), so there is no API surface — every internal header is reachable.
- `Instance` already has the inbound hook set: `onUpdate(float)` (`Engine/src/scene/instances/Instance.h:87`), `onReady`, `onLink`, plus `ready()` and tick phases. `SceneComponent` adds `onAttach` / `onDetach`.
- `Instance::readClassName` already reads a class name from a document, so deserialization is one factory lookup away from constructing user classes.
- `TypeInfo` carries a class name and a base pointer, and no methods or properties.
- No scripting of any kind exists.

---

## 3. Lua first

The one firm outcome. Lua over AngelScript on maintenance rather than syntax: AngelScript is effectively one maintainer with a handful of shipping users, while Lua has multiple independent implementations and **sol2** as a good modern-C++ binding layer.

What is given up is real — Lua is dynamically typed, so a typo is a runtime nil rather than a compile error, where AngelScript is statically typed with C++-ish syntax.

The argument that settled it is **hot reload**, which is the reason a VM earns its complexity at all. If iteration still costs a rebuild, the VM has bought nothing. This also means hot reload is Lua's job and probably never native code's — see the note on unloading in section 6.

What a binding is, since it grounds the harder C++ question: a Lua value holding a C++ object is a **userdata**, a blob containing a pointer, carrying a metatable whose `__index` resolves `obj.add`. What that finds is a plain `int (*)(lua_State *)` that pops arguments off the Lua stack, casts the userdata back, calls the real method, and pushes results. sol2 writes those functions from `new_usertype<T>("T", "add", &T::addChild)`. It is all one binary, one process — a binding is a function pointer in a table, and nothing is dynamic.

### What the dependency actually is

sol2 is header-only and carries no VM, so Lua is a separate dependency we build ourselves. There is also no knob telling sol2 which Lua to bind to — `SOL_LUA_VERSION` is `LUA_VERSION_NUM` read out of whichever `lua.h` is on the include path (`compatibility/lua_version.hpp:106`), and the compat layer `#error`s outside 5.1 to 5.4 (`compat-5.3.h:408`).

That caps us at **Lua 5.4.8** even though 5.5 is released, since sol2's `develop` branch has the same guard. sol2 itself is at **v3.5.0**; the v4.0.0 alpha is not worth taking.

**Lua is compiled as C++.** Built as C its error mechanism is `longjmp`, which jumps past the destructors of every C++ frame it unwinds through, and those frames are sol2's and ours. `ldo.c` picks a `throw`/`catch` implementation instead when `__cplusplus` is defined, and sol2's `SOL_USING_CXX_LUA` makes it include the headers unmangled to match.

The consequence for the no-exceptions rule: Lua throws internally and sol2 throws `sol::error` by default, so engine code has to go through `sol::protected_function` and check the result rather than let anything propagate. `SOL_NO_EXCEPTIONS` is the wrong trade — it turns an unhandled script error into `abort()`.

Undecided: a Lua state is not thread-safe, so given the fiber job system there is a choice between one state per world pinned to a known thread, or something finer. Worth settling early since it is hard to reverse.

---

## 4. Extending TypeInfo

The other direction worth keeping. `TypeInfo` already registers class names and bases; grown to carry methods and properties it becomes one registry feeding **Lua bindings, serialization, the editor details panel, and a C API if one is ever needed**.

That is what Godot's ClassDB and Unreal's UHT are — one source of truth, many consumers. It is also wanted for the details panel regardless of scripting, so it is shared work rather than a scripting-specific cost.

The three ways engines populate such a registry:

- **Parse the headers.** Unreal's UHT reads `UCLASS()` / `UFUNCTION()` macros and emits `.generated.h`. The sane modern version is libclang, walking the real AST rather than writing a C++ parser.
- **Register in code.** Godot's classes call `ClassDB::bind_method` at startup; the registry is dumped to `extension_api.json` and godot-cpp generates its wrapper from that.
- **Templates, no generator.** sol2's `new_usertype<T>(...)`. Works for Lua; cannot work for a C API, since template instantiation cannot produce `extern "C"` symbol names.

Its own design pass when we get there.

### What TypeInfo gets now

One field, `uint16_t id`, assigned from a counter in the constructor.

A script reaching an object holds an `Instance *`, so pushing the handle for what that object *actually* is means dispatching on its runtime type. That dispatch does not belong on `TypeInfo` as a `makeLuaHandle` function pointer: a second language would then be a second field, widening every type for a language it does not use, and `TypeInfo` would own a list of every language that exists.

Instead each language layer keeps its own table, `std::vector<LuaHandlePush>` indexed by `type.id`. Lookup is one indexed load. Adding a language is a directory, not a field.

An unregistered type reaches a script as `nil`. It does not fall back to the nearest bound ancestor, because a missing registration means the class was either not wanted or forgotten, and quietly handing back a base hides both.

So every class a script can reach is registered, including ones that add nothing of their own. A snapshot like `.children` skips what it cannot push rather than writing `nil` into it, since a Lua array with a hole in it has no length.

The id is assigned in first-call order, since each `TypeInfo` is a function-local static built when its class is first touched. It is not stable across runs and must never be serialized — `name` stays the serialized identity.

### Where a script's source lives

On the `ScriptComponent`, serialized inline into whatever document authored the object. Not an asset.

The asset system was considered and rejected: of what it offers, a script wants only a shared identity, and would have to suppress the checksummed `.rasset` container that rejects an out-of-band edit, eviction, async status, import by extension, the frame-in-flight deferred free, and a path index keyed on the wrong file. That is six suppressions to buy one identifier the owning object already has.

The objection to inline storage is that a prefab placed a hundred times writes its script a hundred times. That is true and it is not a script problem: `spawnSubtree` records nothing about the asset it came from, so a placed tree writes out its class, name, every component and every child in full. Script text is one field riding along, and the fix is a placed instance being a reference to its prefab plus per-instance overrides. Script text needs no override, since it is not per-instance mutable. That work is its own design pass.

A shared library reached through `require` is the opposite case — standalone, shared, referenced by name — and every asset feature applies to it. So `ASSET_SCRIPT` is for that kind, when `require` lands, and not for behaviour scripts.

### Where the Lua layer lives

```
Engine/src/scripting/lua/
    LuaRuntime.h/.cpp        one lua_State per world, coroutine scheduler
    LuaHandle.h              the handle struct and its resolve
    LuaTypeBindings.h/.cpp   the id-indexed push table
    bindings/                one file per group of usertypes
```

Only the `.cpp` files include sol2. `LuaRuntime.h` is reached from `World`, so it holds a pointer to an impl rather than a `sol::state` by value, to keep `sol.hpp` out of an include graph that wide.

Types do not register themselves, since that would put sol2 in `scene/`. `bindings/LuaInstance.cpp` registers `Node3D` next to where it defines the `Node3D` usertype.

---

## 5. User C++ as a loadable addition

The third thing worth keeping, in shape if not in detail.

A user-written `Player : Node3D` overriding `onUpdate` and a Lua script defining `function onUpdate(dt)` could be the same thing in two languages — same hooks, same lifecycle, same attachment, same serialization. If that holds, choosing between them stops being architectural and becomes practical: native for speed or a debugger, Lua for iteration. This is Unreal's C++/Blueprint relationship minus the visual editor.

The hooks for it already exist (`onUpdate`, `onReady`, `onAttach`, `onDetach`). The missing piece is construction by name, so deserialization can turn `"Player"` into a `Player` — `readClassName` already reads the string, so it is a factory entry away.

Sketch of what a module boundary looked like in discussion:

```cpp
// Game/src/GameModule.cpp
extern "C" RAPTURE_MODULE_EXPORT void RaptureRegisterModule(Rapture::ModuleRegistry &registry)
{
    registry.setBuildId(RAPTURE_BUILD_ID);
    registry.registerClass<Player>();
    registry.registerLuaType<Pathfinder>();
}
```

`extern "C"` because C++ name mangling is not standardised and we want exactly one unmangled symbol to `dlsym`. `registerLuaType` would be the same call the engine uses for its own types — from Lua's side there is no difference between an engine type and a user type, so "write pathfinding in C++, call it from Lua" is one line.

---

## 6. The ABI question, unresolved

The problem is not that user code goes stale. It is that **one side of the boundary was compiled by us, in the past, and can never be recompiled by the user**, so mismatches are permanent:

- MSVC's debug CRT adds members to `std::vector` via `_ITERATOR_DEBUG_LEVEL`, changing its size. Ship a release engine, let a user build their module in debug, and every struct containing a vector has different offsets on each side. No error, just wrong memory.
- libstdc++ and libc++ lay out `std::string` differently. GCC's own `_GLIBCXX_USE_CXX11_ABI` did this within one compiler.
- The engine's `operator new` and the module's may resolve to different heaps, so allocating on one side and freeing on the other crashes intermittently.

Two ways out were discussed.

**Mandate the toolchain.** Publish the required compiler, stdlib, standard and CRT flavour, hash them into a build id, stamp it into both sides, refuse to load on mismatch. Converts silent corruption into a clear load-time error, and is a small amount of code. This is what Unreal does — they did not solve ABI, they removed the freedom to get it wrong, and engine upgrades require recompiling your game.

**A pure C boundary**, as Godot's GDExtension does. Buys compiler freedom and cross-version stability. Costs designing and maintaining a C API mirroring the engine, plus a C++ wrapper compiled into the plugin — Godot generates theirs, which is the tell. The discipline is that only **handles, PODs, spans and function pointers** cross; never a C++ type, template, container or exception. Opaque handles are what take struct layout out of the contract, since a definition in the header would let callers bake `sizeof` and member offsets in at their compile time. Containers never cross, spans do, and overloads become distinct names.

The toolchain mandate looked much cheaper for where the engine is now, and a C boundary looked like it only pays for third parties shipping binaries on compilers we do not control. Not settled.

Separately: **unloading a module for hot reload is a lifetime problem, not an ABI one.** Dangling vtable pointers on live objects survive a byte-identical toolchain, which is why Unreal's Live Coding patches machine code in place rather than unloading. A build id does nothing for it.

---

## 7. What shipping a binary implies for linking

Not a decision, but it follows fairly directly. If both the editor and a game module link a `STATIC` engine there are **two copies of every engine global** — two asset managers, two loggers, two allocator states. So a shared engine library looks forced rather than chosen.

The shape that fell out:

```
engine.so     shipped by us
editor.exe    shipped by us. links engine.so, dlopens game.so
game.so       built by the user. loaded by the editor so Play runs their classes
game.exe      built by the user. links engine.so, statically links their own code
```

Worth noticing that the dynamic path is needed **only for the editor**. A shipped game is an executable the user builds themselves, so their code links straight in and no module loading happens — same source, two targets, which is Unreal's modular-for-editor, monolithic-for-shipping split.

A public/private header split is wanted regardless of any of this, since `Engine/src` is currently `PUBLIC` wholesale. It is about API surface rather than ABI, and the useful part is that the compiler then reports every public signature leaking a private type.

---

## 8. Script scope: behaviour, attached to a scene object

Two models exist. A **program** (Roblox `Script`, a startup `.lua`) runs once, reaches anywhere through the tree, and orchestrates. A **behaviour** (Unity MonoBehaviour, Godot script-on-node) *is* a class attached to one object, where `self` is that object and lifecycle hooks are called on it.

Behaviour, definitively. Not the Roblox model.

It is also what makes the two tiers in section 5 interchangeable, since a program-scoped script has no counterpart in the native tier — a C++ subclass is inherently attached to something.

So a `ScriptComponent` on a scene object. `attachComponent` does not dedupe, so several scripts per object works, which is Unity's model rather than Godot's one-script-per-node and fits the component tier we already have. Reaching other objects stays possible through the scene API, just explicit rather than default.

Whether a small secondary tier for orchestration ever appears (Godot's autoloads, Unity's manager singletons) is a separate and much smaller question. It does not reopen this one.

---

## 9. Still open

- Lua state threading model.
- Script properties — serialized, editor-visible fields, which is the same problem as [[Editor Component Fields]] and probably the same solution.
- The ABI route, per section 6.
- Whether the engine goes `SHARED` now or when module loading actually lands.
- How the editor rebuilds and reloads `game.so`, and what happens to live objects of user classes when it does.
- Where reflection lands, and whether it precedes or follows the details panel work.
- Whether a secondary orchestration tier is ever wanted alongside the behaviour scripts of section 8.

---

## 10. Next

Lua integration, next session. It needs neither the shared library nor module loading to be useful, since the editor can host a Lua state today — which is part of why it goes first.

Specced in [[Lua Scripting]].

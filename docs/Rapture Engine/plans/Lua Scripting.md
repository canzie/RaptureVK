# Lua Scripting

## Instance

```
Instance.new(class, parent)   -> SceneObject, parented to the scene root when no parent is given
.id            string        read-only
.name          string        read/write
:isA(name)     -> bool
:destroy()
.onDestroy    EventSignal()
```

## SceneObject : Instance

```
.parent                              SceneObject   read/write, assigning reparents, the root has none and cannot be moved
.children                            array         read-only snapshot
:findChild(name)                     -> SceneObject or nil
:findDescendant(path)                -> SceneObject or nil
:findFirstDescendantOfType(class)    -> SceneObject or nil
.components                          array         read-only snapshot
:getComponent(class)                 -> SceneComponent or nil
:addComponent(class)                 -> SceneComponent
:removeComponent(component)
```

## Node3D : SceneObject

```
.position        Vector3   read/write
.rotationE       Vector3   read/write, euler
.rotationQ       Quat
.scale           Vector3   read/write
.worldPosition   Vector3   read-only
:lookAt(target)            turns to face a world space point
:forward()       -> Vector3   local -Z in world space
:right()         -> Vector3   local +X in world space
:up()            -> Vector3   local +Y in world space
```

## SpringArm3D : Node3D

```
.length                    number   read/write
.followsControlRotation    bool     read/write, aims where its controller aims
```

## SceneComponent : Instance

```
.owner           SceneObject   read-only
```

Concrete components are bound one at a time as they are needed. So far:

```
PhysicsBody3D : SceneComponent
  .velocity        Vector3   read/write

CharacterBody3D : PhysicsBody3D
  .walkSpeed       number    read/write
  :move(direction)           walks at walkSpeed along a world space direction
  :jump()
  :isOnGround()    -> bool
  :teleport(position, rotation)
```

`walkSpeed` and `move` are the two that do not exist in C++ yet. Everything else is already on `CharacterBody3D.h:38-54`. They are what keeps a script from rebuilding a movement vector by hand, in the same way `move_and_slide` and `Humanoid:Move` do.

## Controller : SceneObject

From `Controller.h:29,40,46,56`:

```
.possessed          SceneObject   read-only
.intent             table         read-only, this frame's input
.capturesCursor     bool          read-only
:addYawInput(deg)                 turns the aim to the right
:addPitchInput(deg)               tilts the aim up, held within maxPitch
.controlRotation    Quat          read-only, where the controller is aiming
.controlForward     Vector3       read-only, the aim flattened onto the ground
.controlRight       Vector3       read-only, controlForward turned a quarter right
.maxPitch           number        read/write, degrees
.onPossess          EventSignal(subject)
.onUpdate           EventSignal(dt)
```

The aim lives on the controller so a script never accumulates or clamps its own yaw and pitch. A `SpringArm3D` with `.followsControlRotation` set takes it from there, which is what makes the camera follow without the script touching the arm.

## ScriptComponent : SceneComponent

```
.owner           SceneObject   read-only
```

Reached as the global `script`. The body runs once, when the owner is ready, and that run is where a script does its connecting. There is no lifecycle method to fill in and nothing is wired by name.

A consequence: a script cannot hear its own owner's ready, since the body is that moment. `onDestroy` and everything else are ordinary signals.

## EventSignal

```
:connect(fn)     -> EventConnection
:once(fn)        -> EventConnection
```

A connection does not replay, so a signal that fired before the connect is missed. Every signal reporting a state change is therefore paired with a property holding that state, `onPossess` with `.possessed`, and a script handles what is already there before connecting for what comes later.

## EventConnection

```
:disconnect()
.connected       bool
```

## Vector3

```
Vector3.new(x, y, z)
.x .y .z         number    read/write
:length()        -> number
:normalized()    -> Vector3
:cross(other)    -> Vector3
:dot(other)      -> number
```

Arithmetic through `+ - *` and `/`.

## Quat

```
Quat.fromAxisAngle(axis, radians)
Quat.fromEuler(vector3)
:toEuler()       -> Vector3
```

Composition through `*`, and `quat * vector3` rotates the vector.

## Modules

Reached with `require`, which resolves engine modules and project scripts through the asset system rather than the filesystem.

```
local Scene = require("scene")
  Scene.tick.input           EventSignal(dt)
  Scene.tick.prePhysics      EventSignal(dt)
  Scene.tick.postPhysics     EventSignal(dt)
  Scene.onHierarchyChanged   EventSignal()
  Scene.root                 SceneObject
  Scene:find(path)           -> SceneObject or nil

local Input = require("input")
  Input.current()            -> table: look, move, zoom, jump, orbit, pan

local Physics = require("physics")
  Physics.raycast(from, direction, maxDistance)   -> hit or nil

local World = require("world")
  World.gravity              Vector3   read/write
  World.name                 string    read-only

local Time = require("time")
  Time.now()                 -> number, seconds since the run started
  Time.frame                 number, frames since the run started
```

## Globals

```
script                                  ScriptComponent
log(...) warn(...) error(...)
wait(seconds)
spawn(fn)
delay(seconds, fn)
Vector3
Quat
```

## Withheld

`io`, `os`, `package`, `load`, `loadstring`, `dofile`, `debug`.

Nothing render-side, no materials, no GPU, no asset loading beyond references the editor authored.

## Example

`PlayerController.cpp` as a script.

```lua
local mouseSensitivity = 0.1

local controller = script.owner
local body = nil

local function usePuppet(subject)
    body = subject:getComponent("CharacterBody3D")
    body.walkSpeed = 5.0

    local cameraArm = subject:findFirstDescendantOfType("SpringArm3D")
    if cameraArm then
        cameraArm.followsControlRotation = true
    end
end

if controller.possessed then
    usePuppet(controller.possessed)
end
controller.onPossess:connect(usePuppet)

controller.onUpdate:connect(function(dt)
    local subject = controller.possessed
    if not subject or not body then
        return
    end

    local intent = controller.intent

    controller:addYawInput(intent.look.x * mouseSensitivity)
    controller:addPitchInput(-intent.look.y * mouseSensitivity)

    local walk = controller.controlRight * intent.move.x + controller.controlForward * intent.move.z
    body:move(walk)

    if intent.jump then
        body:jump()
    end

    subject:lookAt(subject.position + walk)
end)
```

Locals rather than fields on a table, because each script instance loads its own chunk and so has its own globals and its own closure state.

No yaw or pitch state, no clamping and no arm rotation, because the controller holds the aim and the arm reads it.

## Open

- Input actions and their events, which need an action layer that does not exist. `ControlInput` is one polled per-frame struct (`ControlInput.h:14-23`). The split when it lands: continuous input stays on `onUpdate`, where `dt` is already in hand, and discrete input becomes events, because the alternative is every script edge-detecting `intent.jump` against last frame by hand. Unreal is the same shape underneath, since an Enhanced Input axis action fires `Triggered` every frame it is non-zero.
- UI, which Roblox scripts and we do not.
- Whether `on`-prefixed methods connect to matching signals automatically or every connection is written out.

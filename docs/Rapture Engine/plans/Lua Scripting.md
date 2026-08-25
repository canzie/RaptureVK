# Lua Scripting

## Instance

```
.id            string        read-only
.name          string        read/write
:isA(name)     -> bool
:destroy()
.onDestroy    EventSignal()
```

## SceneObject : Instance

```
.parent                              SceneObject   read/write, assigning reparents
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

The hooks a scripted controller gets, from `Controller.h:29,40,46,56`:

```
.possessed         SceneObject   read-only
.intent            table         read-only, this frame's input
.capturesCursor    bool          read/write
:onPossess(subject)              called when handed a puppet
:onUpdate(dt)                    called each frame while possessing
```


## ScriptComponent : SceneComponent

```
.owner           SceneObject   read-only
```

`self` inside a method is the script's own instance table, so `self.owner` is the object it is attached to.

## Signal

```
:connect(fn)     -> Connection
:once(fn)        -> Connection
```

## Connection

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
  Scene.tick.input           Signal(dt)
  Scene.tick.prePhysics      Signal(dt)
  Scene.tick.postPhysics     Signal(dt)
  Scene.onHierarchyChanged   Signal()
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
local PlayerController = {}

PlayerController.movementSpeed = 5.0
PlayerController.mouseSensitivity = 0.1
PlayerController.maxPitch = 89.0

local WORLD_UP = Vector3.new(0, 1, 0)

function PlayerController:onPossess(subject)
    self.yaw = -90.0
    self.pitch = 0.0
    self.body = subject:getComponent("CharacterBody3D")
    self.cameraArm = subject:findFirstDescendantOfType("SpringArm3D")
end

function PlayerController:onUpdate(dt)
    local subject = self.possessed
    if not subject then
        return
    end

    local intent = self.intent

    self.yaw = self.yaw + intent.look.x * self.mouseSensitivity
    self.pitch = math.max(-self.maxPitch,
                 math.min(self.maxPitch, self.pitch - intent.look.y * self.mouseSensitivity))

    local turn = Quat.fromAxisAngle(WORLD_UP, math.rad(self.yaw))
    local walk = turn * Vector3.new(intent.move.x, 0, intent.move.z)

    self.body.walkSpeed = self.movementSpeed
    self.body:move(walk)

    if intent.jump then
        self.body:jump()
    end

    subject:lookAt(subject.position + walk)

    if self.cameraArm then
        self.cameraArm.rotationQ = Quat.fromAxisAngle(Vector3.new(1, 0, 0), math.rad(self.pitch))
    end
end

return PlayerController
```

`math.clamp` does not exist in Lua 5.4, only in Luau, hence the `min`/`max` pair.

## Open

- Input actions and their events, which need an action layer that does not exist. `ControlInput` is one polled per-frame struct (`ControlInput.h:14-23`). The split when it lands: continuous input stays on `onUpdate`, where `dt` is already in hand, and discrete input becomes events, because the alternative is every script edge-detecting `intent.jump` against last frame by hand. Unreal is the same shape underneath, since an Enhanced Input axis action fires `Triggered` every frame it is non-zero.
- UI, which Roblox scripts and we do not.
- Whether `on`-prefixed methods connect to matching signals automatically or every connection is written out.

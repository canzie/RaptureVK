-- Drives the possessed puppet from the controller's intent.

local mouseSensitivity = 0.1
local walkSpeed = 5.0

local controller = script.owner
local body = nil

local function usePuppet(subject)
    if not subject then
        body = nil
        return
    end

    body = subject:getComponent("CharacterBody3D")
    if body then
        body.walkSpeed = walkSpeed
    end

    local cameraArm = subject:findFirstDescendantOfType("SpringArm3D")
    if cameraArm then
        cameraArm.followsControlRotation = true
    end

    log("possessed " .. subject.name)
end

if controller.possessed then
    usePuppet(controller.possessed)
end
controller.onPossess:connect(usePuppet)

controller.onUpdate:connect(function(dt)
    local subject = controller.possessed
    if not subject then
        return
    end

    local intent = controller.intent

    controller:addYawInput(intent.look.x * mouseSensitivity)
    controller:addPitchInput(-intent.look.y * mouseSensitivity)

    local walk = controller.controlRight * intent.move.x + controller.controlForward * intent.move.z

    if body then
        body:move(walk)
        if intent.jump then
            body:jump()
        end
    else
        subject.position = subject.position + walk * walkSpeed * dt
    end

    subject:lookAt(subject.position + controller.controlForward)
end)

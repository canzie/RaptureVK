#include "LuaBindings.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Rapture::scripting {

static void s_registerVector2(sol::state_view lua)
{
    lua.new_usertype<glm::vec2>(
        "Vector2",
        "new", sol::factories([](float x, float y) { return glm::vec2(x, y); }),
        "x", &glm::vec2::x,
        "y", &glm::vec2::y,
        "length", [](const glm::vec2 &self) { return glm::length(self); },
        "normalized", [](const glm::vec2 &self) { return glm::normalize(self); },
        "dot", [](const glm::vec2 &self, const glm::vec2 &other) { return glm::dot(self, other); },
        sol::meta_function::addition, [](const glm::vec2 &a, const glm::vec2 &b) { return a + b; },
        sol::meta_function::subtraction, [](const glm::vec2 &a, const glm::vec2 &b) { return a - b; },
        sol::meta_function::unary_minus, [](const glm::vec2 &a) { return -a; },
        sol::meta_function::equal_to, [](const glm::vec2 &a, const glm::vec2 &b) { return a == b; },
        sol::meta_function::multiplication, sol::overload(
            [](const glm::vec2 &a, const glm::vec2 &b) { return a * b; },
            [](const glm::vec2 &a, float s) { return a * s; },
            [](float s, const glm::vec2 &a) { return a * s; }),
        sol::meta_function::division, sol::overload(
            [](const glm::vec2 &a, const glm::vec2 &b) { return a / b; },
            [](const glm::vec2 &a, float s) { return a / s; }),
        sol::meta_function::to_string, [](const glm::vec2 &self) {
            return std::to_string(self.x) + ", " + std::to_string(self.y);
        });
}

static void s_registerVector3(sol::state_view lua)
{
    lua.new_usertype<glm::vec3>(
        "Vector3",
        "new", sol::factories([](float x, float y, float z) { return glm::vec3(x, y, z); }),
        "x", &glm::vec3::x,
        "y", &glm::vec3::y,
        "z", &glm::vec3::z,
        "length", [](const glm::vec3 &self) { return glm::length(self); },
        "normalized", [](const glm::vec3 &self) { return glm::normalize(self); },
        "cross", [](const glm::vec3 &self, const glm::vec3 &other) { return glm::cross(self, other); },
        "dot", [](const glm::vec3 &self, const glm::vec3 &other) { return glm::dot(self, other); },
        sol::meta_function::addition, [](const glm::vec3 &a, const glm::vec3 &b) { return a + b; },
        sol::meta_function::subtraction, [](const glm::vec3 &a, const glm::vec3 &b) { return a - b; },
        sol::meta_function::unary_minus, [](const glm::vec3 &a) { return -a; },
        sol::meta_function::equal_to, [](const glm::vec3 &a, const glm::vec3 &b) { return a == b; },
        sol::meta_function::multiplication, sol::overload(
            [](const glm::vec3 &a, const glm::vec3 &b) { return a * b; },
            [](const glm::vec3 &a, float s) { return a * s; },
            [](float s, const glm::vec3 &a) { return a * s; }),
        sol::meta_function::division, sol::overload(
            [](const glm::vec3 &a, const glm::vec3 &b) { return a / b; },
            [](const glm::vec3 &a, float s) { return a / s; }),
        sol::meta_function::to_string, [](const glm::vec3 &self) {
            return std::to_string(self.x) + ", " + std::to_string(self.y) + ", " + std::to_string(self.z);
        });
}

static void s_registerQuat(sol::state_view lua)
{
    lua.new_usertype<glm::quat>(
        "Quat",
        "fromAxisAngle", sol::factories([](const glm::vec3 &axis, float radians) {
            return glm::angleAxis(radians, glm::normalize(axis));
        }),
        "fromEuler", sol::factories([](const glm::vec3 &euler) { return glm::quat(euler); }),
        "toEuler", [](const glm::quat &self) { return glm::eulerAngles(self); },
        sol::meta_function::multiplication, sol::overload(
            [](const glm::quat &a, const glm::quat &b) { return a * b; },
            [](const glm::quat &a, const glm::vec3 &v) { return a * v; }),
        sol::meta_function::equal_to, [](const glm::quat &a, const glm::quat &b) { return a == b; });
}

void registerMathBindings(sol::state_view lua)
{
    s_registerVector2(lua);
    s_registerVector3(lua);
    s_registerQuat(lua);
}

} // namespace Rapture::scripting

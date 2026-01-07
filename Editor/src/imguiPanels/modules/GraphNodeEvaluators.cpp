#include "GraphNodeEvaluators.h"
#include <glm/glm.hpp>
#include <cmath>

namespace Modules {

// Helper: Check if a type is numeric (for operations like add, mul, etc)
template <typename T>
constexpr bool s_isNumeric()
{
    return std::is_same_v<T, uint32_t> || std::is_same_v<T, uint64_t> || std::is_same_v<T, int32_t> ||
           std::is_same_v<T, int64_t> || std::is_same_v<T, float> || std::is_same_v<T, double> ||
           std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3> || std::is_same_v<T, glm::vec4> ||
           std::is_same_v<T, glm::ivec2> || std::is_same_v<T, glm::ivec3> || std::is_same_v<T, glm::ivec4> ||
           std::is_same_v<T, glm::uvec2> || std::is_same_v<T, glm::uvec3> || std::is_same_v<T, glm::uvec4>;
}

// Helper: Check if type is a vector
template <typename T>
constexpr bool s_isVector()
{
    return std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3> || std::is_same_v<T, glm::vec4> ||
           std::is_same_v<T, glm::ivec2> || std::is_same_v<T, glm::ivec3> || std::is_same_v<T, glm::ivec4> ||
           std::is_same_v<T, glm::uvec2> || std::is_same_v<T, glm::uvec3> || std::is_same_v<T, glm::uvec4>;
}

// INPUT node - outputs are already set externally, nothing to do
bool s_evaluateInput(GraphNode &node)
{
    return true;
}

// OUTPUT node - just copies inputs to outputs
bool s_evaluateOutput(GraphNode &node)
{
    if (node.inputs.size() != node.outputs.size()) {
        return false;
    }

    for (size_t i = 0; i < node.inputs.size(); ++i) {
        node.outputs[i].value = node.inputs[i].value;
        node.outputs[i].pType = node.inputs[i].pType;
    }

    return true;
}

// ADD: a + b
bool s_evaluateAdd(GraphNode &node)
{
    if (node.inputs.size() < 2 || node.outputs.size() < 1) {
        return false;
    }

    auto &a = node.inputs[0];
    auto &b = node.inputs[1];
    auto &out = node.outputs[0];

    if (a.pType != b.pType) {
        return false;
    }

    return std::visit(
        [&](auto &&aVal) -> bool {
            using T = std::decay_t<decltype(aVal)>;
            if constexpr (s_isNumeric<T>()) {
                try {
                    auto bVal = std::get<T>(b.value);
                    out.value = aVal + bVal;
                    out.pType = a.pType;
                    return true;
                } catch (...) {
                    return false;
                }
            }
            return false;
        },
        a.value);
}

// SUBTRACT: a - b
bool s_evaluateSubtract(GraphNode &node)
{
    if (node.inputs.size() < 2 || node.outputs.size() < 1) {
        return false;
    }

    auto &a = node.inputs[0];
    auto &b = node.inputs[1];
    auto &out = node.outputs[0];

    if (a.pType != b.pType) {
        return false;
    }

    return std::visit(
        [&](auto &&aVal) -> bool {
            using T = std::decay_t<decltype(aVal)>;
            if constexpr (s_isNumeric<T>()) {
                try {
                    auto bVal = std::get<T>(b.value);
                    out.value = aVal - bVal;
                    out.pType = a.pType;
                    return true;
                } catch (...) {
                    return false;
                }
            }
            return false;
        },
        a.value);
}

// MULTIPLY: a * b
bool s_evaluateMultiply(GraphNode &node)
{
    if (node.inputs.size() < 2 || node.outputs.size() < 1) {
        return false;
    }

    auto &a = node.inputs[0];
    auto &b = node.inputs[1];
    auto &out = node.outputs[0];

    if (a.pType != b.pType) {
        return false;
    }

    return std::visit(
        [&](auto &&aVal) -> bool {
            using T = std::decay_t<decltype(aVal)>;
            if constexpr (s_isNumeric<T>()) {
                try {
                    auto bVal = std::get<T>(b.value);
                    out.value = aVal * bVal;
                    out.pType = a.pType;
                    return true;
                } catch (...) {
                    return false;
                }
            }
            return false;
        },
        a.value);
}

// DIVIDE: a / b
bool s_evaluateDivide(GraphNode &node)
{
    if (node.inputs.size() < 2 || node.outputs.size() < 1) {
        return false;
    }

    auto &a = node.inputs[0];
    auto &b = node.inputs[1];
    auto &out = node.outputs[0];

    if (a.pType != b.pType) {
        return false;
    }

    return std::visit(
        [&](auto &&aVal) -> bool {
            using T = std::decay_t<decltype(aVal)>;
            if constexpr (s_isNumeric<T>()) {
                try {
                    auto bVal = std::get<T>(b.value);
                    out.value = aVal / bVal;
                    out.pType = a.pType;
                    return true;
                } catch (...) {
                    return false;
                }
            }
            return false;
        },
        a.value);
}

// MIX: mix(a, b, alpha) = a * (1 - alpha) + b * alpha
bool s_evaluateMix(GraphNode &node)
{
    if (node.inputs.size() < 3 || node.outputs.size() < 1) {
        return false;
    }

    auto &a = node.inputs[0];
    auto &b = node.inputs[1];
    auto &alpha = node.inputs[2];
    auto &out = node.outputs[0];

    if (a.pType != b.pType) {
        return false;
    }

    return std::visit(
        [&](auto &&aVal) -> bool {
            using T = std::decay_t<decltype(aVal)>;
            if constexpr (s_isNumeric<T>()) {
                try {
                    auto bVal = std::get<T>(b.value);

                    // Alpha should be a scalar float
                    float alphaVal = 0.0f;
                    if (std::holds_alternative<float>(alpha.value)) {
                        alphaVal = std::get<float>(alpha.value);
                    } else if (std::holds_alternative<double>(alpha.value)) {
                        alphaVal = static_cast<float>(std::get<double>(alpha.value));
                    } else {
                        return false;
                    }

                    if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
                        out.value = static_cast<T>(aVal * (1.0 - alphaVal) + bVal * alphaVal);
                    } else if constexpr (std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3> ||
                                         std::is_same_v<T, glm::vec4>) {
                        // Use glm::mix for floating-point vectors
                        out.value = glm::mix(aVal, bVal, alphaVal);
                    } else if constexpr (std::is_same_v<T, glm::ivec2> || std::is_same_v<T, glm::ivec3> ||
                                         std::is_same_v<T, glm::ivec4>) {
                        // Manual mix for integer vectors - convert to float, mix, convert back
                        auto aFloat = glm::vec<T::length(), float>(aVal);
                        auto bFloat = glm::vec<T::length(), float>(bVal);
                        auto mixed = glm::mix(aFloat, bFloat, alphaVal);
                        out.value = T(mixed);
                    } else if constexpr (std::is_same_v<T, glm::uvec2> || std::is_same_v<T, glm::uvec3> ||
                                         std::is_same_v<T, glm::uvec4>) {
                        // Manual mix for unsigned integer vectors
                        auto aFloat = glm::vec<T::length(), float>(aVal);
                        auto bFloat = glm::vec<T::length(), float>(bVal);
                        auto mixed = glm::mix(aFloat, bFloat, alphaVal);
                        out.value = T(mixed);
                    } else {
                        // Integer scalar types
                        out.value = static_cast<T>(aVal * (1.0 - alphaVal) + bVal * alphaVal);
                    }

                    out.pType = a.pType;
                    return true;
                } catch (...) {
                    return false;
                }
            }
            return false;
        },
        a.value);
}

// CLAMP: clamp(value, min, max)
bool s_evaluateClamp(GraphNode &node)
{
    if (node.inputs.size() < 3 || node.outputs.size() < 1) {
        return false;
    }

    auto &value = node.inputs[0];
    auto &minVal = node.inputs[1];
    auto &maxVal = node.inputs[2];
    auto &out = node.outputs[0];

    if (value.pType != minVal.pType || value.pType != maxVal.pType) {
        return false;
    }

    return std::visit(
        [&](auto &&val) -> bool {
            using T = std::decay_t<decltype(val)>;
            if constexpr (s_isNumeric<T>()) {
                try {
                    auto minV = std::get<T>(minVal.value);
                    auto maxV = std::get<T>(maxVal.value);

                    if constexpr (s_isVector<T>()) {
                        out.value = glm::clamp(val, minV, maxV);
                    } else {
                        out.value = std::clamp(val, minV, maxV);
                    }

                    out.pType = value.pType;
                    return true;
                } catch (...) {
                    return false;
                }
            }
            return false;
        },
        value.value);
}

// LENGTH: returns scalar length of a vector (only works on floating-point vectors)
bool s_evaluateLength(GraphNode &node)
{
    if (node.inputs.size() < 1 || node.outputs.size() < 1) {
        return false;
    }

    auto &input = node.inputs[0];
    auto &out = node.outputs[0];

    return std::visit(
        [&](auto &&val) -> bool {
            using T = std::decay_t<decltype(val)>;
            // Only compute length for floating-point vectors
            if constexpr (std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3> ||
                          std::is_same_v<T, glm::vec4>) {
                out.value = glm::length(val);
                out.pType = ParameterType::F32;
                return true;
            }
            return false;
        },
        input.value);
}

// NORMALIZE: normalize a vector (only works on floating-point vectors)
bool s_evaluateNormalize(GraphNode &node)
{
    if (node.inputs.size() < 1 || node.outputs.size() < 1) {
        return false;
    }

    auto &input = node.inputs[0];
    auto &out = node.outputs[0];

    return std::visit(
        [&](auto &&val) -> bool {
            using T = std::decay_t<decltype(val)>;
            // Only normalize floating-point vectors
            if constexpr (std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3> ||
                          std::is_same_v<T, glm::vec4>) {
                out.value = glm::normalize(val);
                out.pType = input.pType;
                return true;
            }
            return false;
        },
        input.value);
}

// SPLIT: vec4 -> x, y, z, w (or vec3 -> x, y, z, or vec2 -> x, y)
bool s_evaluateSplit(GraphNode &node)
{
    if (node.inputs.size() < 1) {
        return false;
    }

    auto &input = node.inputs[0];

    return std::visit(
        [&](auto &&val) -> bool {
            using T = std::decay_t<decltype(val)>;

            if constexpr (std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::ivec2> ||
                          std::is_same_v<T, glm::uvec2>) {
                if (node.outputs.size() < 2)
                    return false;
                node.outputs[0].value = val.x;
                node.outputs[1].value = val.y;
                node.outputs[0].pType = ParameterType::F32;
                node.outputs[1].pType = ParameterType::F32;
                return true;
            } else if constexpr (std::is_same_v<T, glm::vec3> || std::is_same_v<T, glm::ivec3> ||
                                 std::is_same_v<T, glm::uvec3>) {
                if (node.outputs.size() < 3)
                    return false;
                node.outputs[0].value = val.x;
                node.outputs[1].value = val.y;
                node.outputs[2].value = val.z;
                node.outputs[0].pType = ParameterType::F32;
                node.outputs[1].pType = ParameterType::F32;
                node.outputs[2].pType = ParameterType::F32;
                return true;
            } else if constexpr (std::is_same_v<T, glm::vec4> || std::is_same_v<T, glm::ivec4> ||
                                 std::is_same_v<T, glm::uvec4>) {
                if (node.outputs.size() < 4)
                    return false;
                node.outputs[0].value = val.x;
                node.outputs[1].value = val.y;
                node.outputs[2].value = val.z;
                node.outputs[3].value = val.w;
                node.outputs[0].pType = ParameterType::F32;
                node.outputs[1].pType = ParameterType::F32;
                node.outputs[2].pType = ParameterType::F32;
                node.outputs[3].pType = ParameterType::F32;
                return true;
            }

            return false;
        },
        input.value);
}

// GROUP: x, y, z, w -> vec4 (or x, y, z -> vec3, or x, y -> vec2)
bool s_evaluateGroup(GraphNode &node)
{
    if (node.outputs.size() < 1) {
        return false;
    }

    auto &out = node.outputs[0];

    if (node.inputs.size() == 2) {
        // vec2
        return std::visit(
            [&](auto &&xVal) -> bool {
                using T = std::decay_t<decltype(xVal)>;
                if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
                    return std::visit(
                        [&](auto &&yVal) -> bool {
                            using Y = std::decay_t<decltype(yVal)>;
                            if constexpr (std::is_same_v<Y, float> || std::is_same_v<Y, double>) {
                                out.value = glm::vec2(static_cast<float>(xVal), static_cast<float>(yVal));
                                out.pType = ParameterType::VEC2;
                                return true;
                            }
                            return false;
                        },
                        node.inputs[1].value);
                } else if constexpr (std::is_same_v<T, int32_t> || std::is_same_v<T, int64_t>) {
                    return std::visit(
                        [&](auto &&yVal) -> bool {
                            using Y = std::decay_t<decltype(yVal)>;
                            if constexpr (std::is_same_v<Y, int32_t> || std::is_same_v<Y, int64_t>) {
                                out.value = glm::ivec2(static_cast<int32_t>(xVal), static_cast<int32_t>(yVal));
                                out.pType = ParameterType::IVEC2;
                                return true;
                            }
                            return false;
                        },
                        node.inputs[1].value);
                } else if constexpr (std::is_same_v<T, uint32_t> || std::is_same_v<T, uint64_t>) {
                    return std::visit(
                        [&](auto &&yVal) -> bool {
                            using Y = std::decay_t<decltype(yVal)>;
                            if constexpr (std::is_same_v<Y, uint32_t> || std::is_same_v<Y, uint64_t>) {
                                out.value = glm::uvec2(static_cast<uint32_t>(xVal), static_cast<uint32_t>(yVal));
                                out.pType = ParameterType::UVEC2;
                                return true;
                            }
                            return false;
                        },
                        node.inputs[1].value);
                }
                return false;
            },
            node.inputs[0].value);
    } else if (node.inputs.size() == 3) {
        // vec3
        float x = 0.0f, y = 0.0f, z = 0.0f;
        bool allFloat = true;
        int32_t ix = 0, iy = 0, iz = 0;
        bool allInt = true;
        uint32_t ux = 0, uy = 0, uz = 0;
        bool allUint = true;

        for (int i = 0; i < 3; ++i) {
            std::visit(
                [&](auto &&val) {
                    using T = std::decay_t<decltype(val)>;
                    if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
                        if (i == 0)
                            x = static_cast<float>(val);
                        else if (i == 1)
                            y = static_cast<float>(val);
                        else
                            z = static_cast<float>(val);
                    } else {
                        allFloat = false;
                    }

                    if constexpr (std::is_same_v<T, int32_t> || std::is_same_v<T, int64_t>) {
                        if (i == 0)
                            ix = static_cast<int32_t>(val);
                        else if (i == 1)
                            iy = static_cast<int32_t>(val);
                        else
                            iz = static_cast<int32_t>(val);
                    } else {
                        allInt = false;
                    }

                    if constexpr (std::is_same_v<T, uint32_t> || std::is_same_v<T, uint64_t>) {
                        if (i == 0)
                            ux = static_cast<uint32_t>(val);
                        else if (i == 1)
                            uy = static_cast<uint32_t>(val);
                        else
                            uz = static_cast<uint32_t>(val);
                    } else {
                        allUint = false;
                    }
                },
                node.inputs[i].value);
        }

        if (allFloat) {
            out.value = glm::vec3(x, y, z);
            out.pType = ParameterType::VEC3;
            return true;
        } else if (allInt) {
            out.value = glm::ivec3(ix, iy, iz);
            out.pType = ParameterType::IVEC3;
            return true;
        } else if (allUint) {
            out.value = glm::uvec3(ux, uy, uz);
            out.pType = ParameterType::UVEC3;
            return true;
        }
    } else if (node.inputs.size() == 4) {
        // vec4
        float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
        bool allFloat = true;
        int32_t ix = 0, iy = 0, iz = 0, iw = 0;
        bool allInt = true;
        uint32_t ux = 0, uy = 0, uz = 0, uw = 0;
        bool allUint = true;

        for (int i = 0; i < 4; ++i) {
            std::visit(
                [&](auto &&val) {
                    using T = std::decay_t<decltype(val)>;
                    if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
                        if (i == 0)
                            x = static_cast<float>(val);
                        else if (i == 1)
                            y = static_cast<float>(val);
                        else if (i == 2)
                            z = static_cast<float>(val);
                        else
                            w = static_cast<float>(val);
                    } else {
                        allFloat = false;
                    }

                    if constexpr (std::is_same_v<T, int32_t> || std::is_same_v<T, int64_t>) {
                        if (i == 0)
                            ix = static_cast<int32_t>(val);
                        else if (i == 1)
                            iy = static_cast<int32_t>(val);
                        else if (i == 2)
                            iz = static_cast<int32_t>(val);
                        else
                            iw = static_cast<int32_t>(val);
                    } else {
                        allInt = false;
                    }

                    if constexpr (std::is_same_v<T, uint32_t> || std::is_same_v<T, uint64_t>) {
                        if (i == 0)
                            ux = static_cast<uint32_t>(val);
                        else if (i == 1)
                            uy = static_cast<uint32_t>(val);
                        else if (i == 2)
                            uz = static_cast<uint32_t>(val);
                        else
                            uw = static_cast<uint32_t>(val);
                    } else {
                        allUint = false;
                    }
                },
                node.inputs[i].value);
        }

        if (allFloat) {
            out.value = glm::vec4(x, y, z, w);
            out.pType = ParameterType::VEC4;
            return true;
        } else if (allInt) {
            out.value = glm::ivec4(ix, iy, iz, iw);
            out.pType = ParameterType::IVEC4;
            return true;
        } else if (allUint) {
            out.value = glm::uvec4(ux, uy, uz, uw);
            out.pType = ParameterType::UVEC4;
            return true;
        }
    }

    return false;
}

void initializeEvaluators(std::unordered_map<NodeOpType, NodeEvaluator> &evaluators)
{
    evaluators[NodeOpType::INPUT] = s_evaluateInput;
    evaluators[NodeOpType::OUTPUT] = s_evaluateOutput;
    evaluators[NodeOpType::ADD] = s_evaluateAdd;
    evaluators[NodeOpType::SUBTRACT] = s_evaluateSubtract;
    evaluators[NodeOpType::MULTIPLY] = s_evaluateMultiply;
    evaluators[NodeOpType::DIVIDE] = s_evaluateDivide;
    evaluators[NodeOpType::MIX] = s_evaluateMix;
    evaluators[NodeOpType::CLAMP] = s_evaluateClamp;
    evaluators[NodeOpType::LENGTH] = s_evaluateLength;
    evaluators[NodeOpType::NORMALIZE] = s_evaluateNormalize;
    evaluators[NodeOpType::SPLIT] = s_evaluateSplit;
    evaluators[NodeOpType::GROUP] = s_evaluateGroup;
}

} // namespace Modules

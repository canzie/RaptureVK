#ifndef RAPTURE__PROCEDURAL_TEXTURES_H
#define RAPTURE__PROCEDURAL_TEXTURES_H

#include <glm/glm.hpp>

#include <string>
#include <variant>
#include <vector>

namespace Rapture {

using PushConstantInputValue = std::variant<int32_t, uint32_t, bool, float, glm::vec2, glm::vec3, glm::vec4, glm::mat3, glm::mat4>;

struct PushConstantInput {
    std::string name;
    PushConstantInputValue value;
};

class ProceduralTexture {
  public:
    ProceduralTexture(const std::string &shaderPath, const std::vector<PushConstantInput> &pushConstantInputs);
    ~ProceduralTexture();
};

} // namespace Rapture

#endif // RAPTURE__PROCEDURAL_TEXTURES_H
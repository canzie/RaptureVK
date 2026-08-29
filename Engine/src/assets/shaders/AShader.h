#ifndef RAPTURE__ASHADER_H
#define RAPTURE__ASHADER_H

#include "assets/asset_manager/Asset.h"
#include "gpu/shaders/Shader.h"

#include <memory>

namespace Rapture {

/**
 * @brief A shader asset, held as the compiled stages a pipeline is built from
 */
class AShader : public Asset {
  public:
    explicit AShader(std::unique_ptr<Shader> shader);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    std::vector<uint8_t> serialize() const override;

    Shader &shader() { return *m_shader; }
    const Shader &shader() const { return *m_shader; }

    Shader *operator->() const { return m_shader.get(); }

  private:
    std::unique_ptr<Shader> m_shader;
};

} // namespace Rapture

#endif // RAPTURE__ASHADER_H

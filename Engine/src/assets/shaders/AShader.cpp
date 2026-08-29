#include "AShader.h"

namespace Rapture {

AShader::AShader(std::unique_ptr<Shader> shader) : m_shader(std::move(shader)) {}

const TypeInfo &AShader::staticType()
{
    static const TypeInfo type("AShader", &Asset::staticType());
    return type;
}

const TypeInfo &AShader::type() const
{
    return staticType();
}

std::vector<uint8_t> AShader::serialize() const
{
    // TODO: write the compiled SPIR-V here so a shader is not recompiled from source on every run
    return {};
}

} // namespace Rapture

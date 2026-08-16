#ifndef RAPTURE__SHADER_COMPILATION_H
#define RAPTURE__SHADER_COMPILATION_H

#include <filesystem>
#include <string>
#include <vector>

#include "ShaderCommon.h"

namespace Rapture {

class ShaderCompiler {
  public:
    ShaderCompiler();
    ~ShaderCompiler();

    std::vector<char> Compile(const std::filesystem::path &path, const ShaderCompileInfo &compileInfo);

  private:
    int getShaderStage(const std::filesystem::path &path);
    std::string readFile(const std::filesystem::path &path);
};

} // namespace Rapture

#endif // RAPTURE__SHADER_COMPILATION_H

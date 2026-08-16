#pragma once

#include "platform/PlatformContext.h"

namespace Rapture {

class GlfwContext : public PlatformContext {
  public:
    GlfwContext();
    ~GlfwContext() override;

    GlfwContext(const GlfwContext &) = delete;
    GlfwContext &operator=(const GlfwContext &) = delete;
};

} // namespace Rapture

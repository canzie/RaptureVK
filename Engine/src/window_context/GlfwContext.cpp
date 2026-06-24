#include "GlfwContext.h"

#include "utils/rp_assert.h"

#include <GLFW/glfw3.h>

namespace Rapture {

GlfwContext::GlfwContext()
{
    bool initialized = glfwInit();
    RP_ASSERT(initialized, "Failed to initialize GLFW!");
}

GlfwContext::~GlfwContext()
{
    glfwTerminate();
}

std::unique_ptr<PlatformContext> PlatformContext::create()
{
    return std::make_unique<GlfwContext>();
}

} // namespace Rapture

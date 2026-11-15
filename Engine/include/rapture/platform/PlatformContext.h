#pragma once

#include <memory>

namespace Rapture {

class PlatformContext {
  public:
    virtual ~PlatformContext() {}

    static std::unique_ptr<PlatformContext> create();
};

} // namespace Rapture

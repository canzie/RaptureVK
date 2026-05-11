#ifndef RAPTURE__CAMERADATABUFFER_H
#define RAPTURE__CAMERADATABUFFER_H

#include "ObjectDataBase.h"

#include "cameras/CameraCommon.h"

namespace Rapture {

// Forward declarations
struct CameraComponent;
struct TransformComponent;

class CameraDataBuffer : public ObjectDataBuffer {
  public:
    CameraDataBuffer(uint32_t frameCount = 1);

    void onUpdate(const CameraComponent &camera, uint32_t frameIndex = 0);
};

} // namespace Rapture

#endif // RAPTURE__CAMERADATABUFFER_H

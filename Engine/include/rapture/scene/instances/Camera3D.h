#ifndef RAPTURE__CAMERA3D_H
#define RAPTURE__CAMERA3D_H

#include <rapture/scene/instances/Node3D.h>

namespace Rapture {

class Camera3D : public Node3D {
  public:
    Camera3D(Scene &scene, std::string_view name);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    float fieldOfView() const;
    void setFieldOfView(float degrees);

    float nearPlane() const;
    void setNearPlane(float nearPlane);

    float farPlane() const;
    void setFarPlane(float farPlane);

    float exposureStops() const;

    /**
     * @brief Sets the exposure applied before tone mapping
     * @param stops Exposure in f-stops, each one doubling the brightness
     */
    void setExposureStops(float stops);

    void serialize(WriteNode node) const override;
    void deserialize(ReadNode node) override;

  protected:
    void onWorldTransformChanged() override;
};

} // namespace Rapture

#endif // RAPTURE__CAMERA3D_H

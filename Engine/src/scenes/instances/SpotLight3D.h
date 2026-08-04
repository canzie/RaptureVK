#ifndef RAPTURE__SPOTLIGHT3D_H
#define RAPTURE__SPOTLIGHT3D_H

#include "scenes/instances/Light3D.h"

namespace Rapture {

class SpotLight3D : public Light3D {
  public:
    SpotLight3D(Scene &scene, std::string_view name);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    float range() const;
    void setRange(float range);

    float innerConeAngle() const;
    void setInnerConeAngle(float radians);

    float outerConeAngle() const;
    void setOuterConeAngle(float radians);

    void setCastsShadow(bool castsShadow) override;

    void serialize(WriteNode node) const override;
    void deserialize(ReadNode node) override;
};

} // namespace Rapture

#endif // RAPTURE__SPOTLIGHT3D_H

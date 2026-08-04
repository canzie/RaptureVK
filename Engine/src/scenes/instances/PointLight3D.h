#ifndef RAPTURE__POINTLIGHT3D_H
#define RAPTURE__POINTLIGHT3D_H

#include "scenes/instances/Light3D.h"

namespace Rapture {

class PointLight3D : public Light3D {
  public:
    PointLight3D(Scene &scene, std::string_view name);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    float range() const;
    void setRange(float range);

    void setCastsShadow(bool castsShadow) override;

    void serialize(WriteNode node) const override;
    void deserialize(ReadNode node) override;
};

} // namespace Rapture

#endif // RAPTURE__POINTLIGHT3D_H

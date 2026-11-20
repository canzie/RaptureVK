#ifndef RAPTURE__DIRECTIONALLIGHT3D_H
#define RAPTURE__DIRECTIONALLIGHT3D_H

#include <rapture/scene/instances/Light3D.h>

namespace Rapture {

class DirectionalLight3D : public Light3D {
  public:
    DirectionalLight3D(Scene &scene, std::string_view name);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    bool isAtmosphereSun() const;
    void setAtmosphereSun(bool atmosphereSun);

    void setCastsShadow(bool castsShadow) override;

    /**
     * @brief Sets how far from the viewer this light's shadows reach
     * @param distance World units, used while the light casts shadows
     */
    void setShadowDistance(float distance);

    void serialize(WriteNode node) const override;
    void deserialize(ReadNode node) override;

  protected:
    void applyShadowSettings(ReadNode node) override;
};

} // namespace Rapture

#endif // RAPTURE__DIRECTIONALLIGHT3D_H

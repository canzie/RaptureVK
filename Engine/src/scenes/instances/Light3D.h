#ifndef RAPTURE__LIGHT3D_H
#define RAPTURE__LIGHT3D_H

#include "scenes/entities/EntityCommon.h"
#include "scenes/instances/Node3D.h"

namespace Rapture {

/**
 * @brief Shared surface of every light, reaching the light component the concrete class attached.
 */
class Light3D : public Node3D {
  public:
    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    glm::vec3 color() const;
    void setColor(const glm::vec3 &color);

    float intensity() const;
    void setIntensity(float intensity);

    bool usesTemperature() const;
    void setUsesTemperature(bool usesTemperature);

    float temperature() const;
    void setTemperature(float temperature);

    /**
     * @brief Converts a colour temperature to linear RGB
     * @param kelvin Temperature in kelvin, clamped to the 1000 to 40000 range
     * @return The matching colour
     */
    static glm::vec3 kelvinToRgb(float kelvin);

    bool isActive() const;
    void setActive(bool active);

    Mobility mobility() const;
    void setMobility(Mobility mobility);

    bool castsShadow() const;

    /**
     * @brief Turns shadow casting on or off, attaching or removing the shadow this light type uses
     * @param castsShadow Whether the light should cast a shadow
     */
    virtual void setCastsShadow(bool castsShadow) = 0;

    void serialize(WriteNode node) const override;
    void deserialize(ReadNode node) override;

  protected:
    Light3D(Scene &scene, std::string_view name);

    /**
     * @brief Writes the authored colour and temperature into the light component as one colour
     */
    void applyColor();

  private:
    glm::vec3 m_color = glm::vec3(1.0f, 0.8f, 0.6f);
    bool m_usesTemperature = false;
    float m_temperature = 6500.0f;
};

} // namespace Rapture

#endif // RAPTURE__LIGHT3D_H

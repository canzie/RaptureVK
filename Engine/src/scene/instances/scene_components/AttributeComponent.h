#ifndef RAPTURE__ATTRIBUTE_COMPONENT_H
#define RAPTURE__ATTRIBUTE_COMPONENT_H

#include "core/named_values/Attributes.h"
#include "scene/instances/SceneComponent.h"

namespace Rapture {

/**
 * @brief The attributes put on the object this is attached to.
 */
class AttributeComponent : public SceneComponent {
  public:
    AttributeComponent(Scene &scene, std::string_view name);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    Attributes &attributes() { return m_attributes; }
    const Attributes &attributes() const { return m_attributes; }

    void serialize(WriteNode node) const override;
    void deserialize(ReadNode node) override;

  private:
    Attributes m_attributes;
};

} // namespace Rapture

#endif // RAPTURE__ATTRIBUTE_COMPONENT_H

#ifndef RAPTURE__VISIBILITY_COMPONENT_H
#define RAPTURE__VISIBILITY_COMPONENT_H

#include "scenes/instances/SceneComponent.h"

namespace Rapture {

/**
 * @brief Where the object this is attached to is shown.
 */
class VisibilityComponent : public SceneComponent {
  public:
    VisibilityComponent(Scene &scene, std::string_view name);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    void serialize(WriteNode node) const override;
    void deserialize(ReadNode node) override;

  public:
    bool inOutliner = true;
};

} // namespace Rapture

#endif // RAPTURE__VISIBILITY_COMPONENT_H

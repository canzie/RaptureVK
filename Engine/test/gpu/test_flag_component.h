#ifndef RAPTURE__TEST_FLAG_COMPONENT_H
#define RAPTURE__TEST_FLAG_COMPONENT_H

#include <rapture/core/serialization/SerialDocument.h>
#include <rapture/core/utils/TypeInfo.h>
#include <rapture/scene/instances/InstanceRegistry.h>
#include <rapture/scene/instances/SceneComponent.h>

#include <string_view>

/**
 * @brief A scene component for tests, holding one flag it writes and reads back
 */
class TestFlagComponent : public Rapture::SceneComponent {
  public:
    TestFlagComponent(Rapture::Scene &scene, std::string_view name) : SceneComponent(scene, name) {}

    static const Rapture::TypeInfo &staticType()
    {
        static const Rapture::TypeInfo type("TestFlagComponent", &Rapture::SceneComponent::staticType());
        return type;
    }

    const Rapture::TypeInfo &type() const override { return staticType(); }

    void serialize(Rapture::WriteNode node) const override
    {
        SceneComponent::serialize(node);
        node.set(KEY_FLAG, flag);
    }

    void deserialize(Rapture::ReadNode node) override
    {
        SceneComponent::deserialize(node);
        flag = node.child(KEY_FLAG).asBool(true);
    }

    /**
     * @brief Registers the class, so a document naming it can be read back
     */
    static void registerClass()
    {
        if (!Rapture::InstanceRegistry::containsComponent(staticType().name)) {
            Rapture::InstanceRegistry::addComponent<TestFlagComponent>();
        }
    }

  public:
    bool flag = true;

  private:
    static constexpr std::string_view KEY_FLAG = "flag";
};

#endif // RAPTURE__TEST_FLAG_COMPONENT_H

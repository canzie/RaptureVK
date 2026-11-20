#include "test_flag_component.h"
#include "windowless_application_environment.h"

#include <rapture/core/serialization/SerialDocument.h>
#include <rapture/scene/Scene.h>
#include <rapture/scene/instances/Node3D.h>

class SceneRestore : public WindowlessGpuTest {
  protected:
    void SetUp() override
    {
        WindowlessGpuTest::SetUp();
        TestFlagComponent::registerClass();
    }
};

TEST_F(SceneRestore, DropsComponentsAddedAfterTheSnapshot)
{
    Rapture::Scene scene("SceneRestore");
    auto *node = scene.root()->add<Rapture::Node3D>("Node");

    Rapture::SerialDocument snapshot = scene.snapshot();
    node->addComponent<TestFlagComponent>();

    ASSERT_TRUE(scene.restoreFrom(snapshot.rootView()));

    EXPECT_TRUE(node->components().empty());
}

TEST_F(SceneRestore, BringsBackRemovedComponentsWithTheirFields)
{
    Rapture::Scene scene("SceneRestore");
    auto *node = scene.root()->add<Rapture::Node3D>("Node");
    node->addComponent<TestFlagComponent>()->flag = false;

    Rapture::SerialDocument snapshot = scene.snapshot();
    node->removeComponent<TestFlagComponent>();

    ASSERT_TRUE(scene.restoreFrom(snapshot.rootView()));

    auto *restored = node->component<TestFlagComponent>();
    ASSERT_NE(restored, nullptr);
    EXPECT_FALSE(restored->flag);
    EXPECT_EQ(node->components().size(), 1u);
}

TEST_F(SceneRestore, RevertsChangedComponentFields)
{
    Rapture::Scene scene("SceneRestore");
    auto *node = scene.root()->add<Rapture::Node3D>("Node");
    node->addComponent<TestFlagComponent>()->flag = false;

    Rapture::SerialDocument snapshot = scene.snapshot();
    node->component<TestFlagComponent>()->flag = true;

    ASSERT_TRUE(scene.restoreFrom(snapshot.rootView()));

    auto *restored = node->component<TestFlagComponent>();
    ASSERT_NE(restored, nullptr);
    EXPECT_FALSE(restored->flag);
}

TEST_F(SceneRestore, DestroyedObjectComesBackWithItsComponents)
{
    Rapture::Scene scene("SceneRestore");
    auto *node = scene.root()->add<Rapture::Node3D>("Node");
    node->addComponent<TestFlagComponent>()->flag = false;
    const Rapture::InstanceId id = node->id();

    Rapture::SerialDocument snapshot = scene.snapshot();
    scene.destroyInstance(node);

    ASSERT_TRUE(scene.restoreFrom(snapshot.rootView()));

    Rapture::SceneObject *restored = scene.root()->findChild("Node");
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->id(), id);

    auto *component = restored->component<TestFlagComponent>();
    ASSERT_NE(component, nullptr);
    EXPECT_FALSE(component->flag);
}

TEST_F(SceneRestore, RevertsObjectVisibilityAndAttributes)
{
    Rapture::Scene scene("SceneRestore");
    auto *node = scene.root()->add<Rapture::Node3D>("Node");
    node->visibility().inOutliner = false;
    node->attributes().setFloat("speed", 2.5f);

    Rapture::SerialDocument snapshot = scene.snapshot();
    node->visibility().inOutliner = true;
    node->attributes().setFloat("speed", 1.0f);
    node->attributes().setBool("added", true);

    ASSERT_TRUE(scene.restoreFrom(snapshot.rootView()));

    EXPECT_FALSE(node->visibility().inOutliner);
    EXPECT_FLOAT_EQ(node->attributes().getFloat("speed"), 2.5f);
    EXPECT_FALSE(node->attributes().has("added"));
}

TEST_F(SceneRestore, RoundTripMatchesTheSnapshot)
{
    Rapture::Scene scene("SceneRestore");
    auto *parent = scene.root()->add<Rapture::Node3D>("Parent");
    auto *child = parent->add<Rapture::Node3D>("Child");
    child->addComponent<TestFlagComponent>();
    child->setPosition(glm::vec3(1.0f, 2.0f, 3.0f));

    Rapture::SerialDocument snapshot = scene.snapshot();
    const std::string before = snapshot.toText();

    child->setPosition(glm::vec3(-4.0f));
    child->removeComponent<TestFlagComponent>();
    parent->addComponent<TestFlagComponent>();
    scene.root()->add<Rapture::Node3D>("Gained");
    scene.destroyInstance(parent);

    ASSERT_TRUE(scene.restoreFrom(snapshot.rootView()));

    EXPECT_EQ(scene.snapshot().toText(), before);
}

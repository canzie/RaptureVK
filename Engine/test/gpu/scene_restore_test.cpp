#include "windowless_application_environment.h"

#include <rapture/core/serialization/SerialDocument.h>
#include <rapture/scene/Scene.h>
#include <rapture/scene/instances/Node3D.h>
#include <rapture/scene/instances/scene_components/VisibilityComponent.h>

class SceneRestore : public WindowlessGpuTest {};

TEST_F(SceneRestore, DropsComponentsAddedAfterTheSnapshot)
{
    Rapture::Scene scene("SceneRestore");
    auto *node = scene.root()->add<Rapture::Node3D>("Node");

    Rapture::SerialDocument snapshot = scene.snapshot();
    node->addComponent<Rapture::VisibilityComponent>();

    ASSERT_TRUE(scene.restoreFrom(snapshot.rootView()));

    EXPECT_TRUE(node->components().empty());
}

TEST_F(SceneRestore, BringsBackRemovedComponentsWithTheirFields)
{
    Rapture::Scene scene("SceneRestore");
    auto *node = scene.root()->add<Rapture::Node3D>("Node");
    node->addComponent<Rapture::VisibilityComponent>()->inOutliner = false;

    Rapture::SerialDocument snapshot = scene.snapshot();
    node->removeComponent<Rapture::VisibilityComponent>();

    ASSERT_TRUE(scene.restoreFrom(snapshot.rootView()));

    auto *restored = node->component<Rapture::VisibilityComponent>();
    ASSERT_NE(restored, nullptr);
    EXPECT_FALSE(restored->inOutliner);
    EXPECT_EQ(node->components().size(), 1u);
}

TEST_F(SceneRestore, RevertsChangedComponentFields)
{
    Rapture::Scene scene("SceneRestore");
    auto *node = scene.root()->add<Rapture::Node3D>("Node");
    node->addComponent<Rapture::VisibilityComponent>()->inOutliner = false;

    Rapture::SerialDocument snapshot = scene.snapshot();
    node->component<Rapture::VisibilityComponent>()->inOutliner = true;

    ASSERT_TRUE(scene.restoreFrom(snapshot.rootView()));

    auto *restored = node->component<Rapture::VisibilityComponent>();
    ASSERT_NE(restored, nullptr);
    EXPECT_FALSE(restored->inOutliner);
}

TEST_F(SceneRestore, DestroyedObjectComesBackWithItsComponents)
{
    Rapture::Scene scene("SceneRestore");
    auto *node = scene.root()->add<Rapture::Node3D>("Node");
    node->addComponent<Rapture::VisibilityComponent>()->inOutliner = false;
    const Rapture::InstanceId id = node->id();

    Rapture::SerialDocument snapshot = scene.snapshot();
    scene.destroyInstance(node);

    ASSERT_TRUE(scene.restoreFrom(snapshot.rootView()));

    Rapture::SceneObject *restored = scene.root()->findChild("Node");
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->id(), id);

    auto *component = restored->component<Rapture::VisibilityComponent>();
    ASSERT_NE(component, nullptr);
    EXPECT_FALSE(component->inOutliner);
}

TEST_F(SceneRestore, RoundTripMatchesTheSnapshot)
{
    Rapture::Scene scene("SceneRestore");
    auto *parent = scene.root()->add<Rapture::Node3D>("Parent");
    auto *child = parent->add<Rapture::Node3D>("Child");
    child->addComponent<Rapture::VisibilityComponent>();
    child->setPosition(glm::vec3(1.0f, 2.0f, 3.0f));

    Rapture::SerialDocument snapshot = scene.snapshot();
    const std::string before = snapshot.toText();

    child->setPosition(glm::vec3(-4.0f));
    child->removeComponent<Rapture::VisibilityComponent>();
    parent->addComponent<Rapture::VisibilityComponent>();
    scene.root()->add<Rapture::Node3D>("Gained");
    scene.destroyInstance(parent);

    ASSERT_TRUE(scene.restoreFrom(snapshot.rootView()));

    EXPECT_EQ(scene.snapshot().toText(), before);
}

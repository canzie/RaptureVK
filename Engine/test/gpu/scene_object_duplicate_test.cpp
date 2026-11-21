#include "registered_assets_gpu_test.h"
#include "scene_document_helpers.h"
#include "test_flag_component.h"

#include <rapture/core/serialization/SerialDocument.h>
#include <rapture/scene/Scene.h>
#include <rapture/scene/instances/Folder.h>
#include <rapture/scene/instances/Node3D.h>
#include <rapture/scene/instances/SkeletalMesh3D.h>
#include <rapture/scene/instances/SkeletonPose.h>

#include <glm/glm.hpp>

#include <memory>

/**
 * @brief A scene object written out and spawned back under a parent is a copy of it with an identity of its own
 */
class SceneObjectDuplicate : public RegisteredAssetsGpuTest {
  protected:
    void SetUp() override
    {
        RegisteredAssetsGpuTest::SetUp();
        TestFlagComponent::registerClass();
    }

    /**
     * @brief Copies a scene object and everything under it in beside the original
     * @param original The scene object to copy
     * @return The copy, or nullptr if it could not be spawned
     */
    static Rapture::SceneObject *duplicate(Rapture::SceneObject &original)
    {
        std::unique_ptr<Rapture::SerialDocument> document = SceneDocument_writeSubtree(original);
        return Rapture::SceneObject::spawnSubtree(*original.parent(), document->rootView());
    }
};

TEST_F(SceneObjectDuplicate, CopyHasNewIdsAndTheSameFields)
{
    Rapture::Scene scene("SceneObjectDuplicate");
    auto *original = scene.root()->add<Rapture::Node3D>("Original");
    original->setPosition(glm::vec3(1.0f, 2.0f, 3.0f));
    original->addComponent<TestFlagComponent>()->flag = false;
    auto *originalChild = original->add<Rapture::Node3D>("Child");

    Rapture::SceneObject *copy = duplicate(*original);
    ASSERT_NE(copy, nullptr);
    auto *copyNode = copy->as<Rapture::Node3D>();
    ASSERT_NE(copyNode, nullptr);

    EXPECT_NE(copy->id(), original->id());
    EXPECT_EQ(copy->parent(), original->parent());
    EXPECT_EQ(copyNode->position(), original->position());

    auto *component = copy->component<TestFlagComponent>();
    ASSERT_NE(component, nullptr);
    EXPECT_FALSE(component->flag);
    EXPECT_NE(component, original->component<TestFlagComponent>());

    ASSERT_EQ(copy->children().size(), 1u);
    Rapture::SceneObject *copyChild = copy->children()[0].get();
    EXPECT_EQ(copyChild->name(), "Child");
    EXPECT_NE(copyChild->id(), originalChild->id());

    ASSERT_EQ(original->children().size(), 1u);
    EXPECT_EQ(original->children()[0].get(), originalChild);
}

TEST_F(SceneObjectDuplicate, ReferenceInsideTheCopyPointsAtTheCopy)
{
    const Rapture::AssetHandle skeleton = registerTwoJointSkeleton("SceneObjectDuplicate.Inside");

    Rapture::Scene scene("SceneObjectDuplicate");
    auto *group = scene.root()->add<Rapture::Folder>("Character");
    auto *pose = addPose(*group, skeleton);
    ASSERT_NE(pose, nullptr);
    auto *mesh = group->add<Rapture::SkeletalMesh3D>("Mesh");
    mesh->setPose(pose);

    Rapture::SceneObject *copy = duplicate(*group);
    ASSERT_NE(copy, nullptr);

    auto *copyPose = copy->findFirstChildOfType<Rapture::SkeletonPose>();
    auto *copyMesh = copy->findFirstChildOfType<Rapture::SkeletalMesh3D>();
    ASSERT_NE(copyPose, nullptr);
    ASSERT_NE(copyMesh, nullptr);

    EXPECT_NE(copyPose, pose);
    EXPECT_EQ(copyMesh->pose(), copyPose);
    EXPECT_EQ(mesh->pose(), pose);
}

TEST_F(SceneObjectDuplicate, ReferenceOutsideTheCopyKeepsPointingAtTheOriginal)
{
    const Rapture::AssetHandle skeleton = registerTwoJointSkeleton("SceneObjectDuplicate.Outside");

    Rapture::Scene scene("SceneObjectDuplicate");
    auto *pose = addPose(*scene.root(), skeleton);
    ASSERT_NE(pose, nullptr);
    auto *mesh = scene.root()->add<Rapture::SkeletalMesh3D>("Mesh");
    mesh->setPose(pose);

    Rapture::SceneObject *copy = duplicate(*mesh);
    ASSERT_NE(copy, nullptr);
    auto *copyMesh = copy->as<Rapture::SkeletalMesh3D>();
    ASSERT_NE(copyMesh, nullptr);

    EXPECT_EQ(copyMesh->pose(), pose);
}

TEST_F(SceneObjectDuplicate, CopyOfACopyIsItsOwnObject)
{
    Rapture::Scene scene("SceneObjectDuplicate");
    auto *original = scene.root()->add<Rapture::Node3D>("Original");

    Rapture::SceneObject *first = duplicate(*original);
    ASSERT_NE(first, nullptr);
    Rapture::SceneObject *second = duplicate(*first);
    ASSERT_NE(second, nullptr);

    EXPECT_NE(second->id(), first->id());
    EXPECT_NE(second->id(), original->id());
    EXPECT_EQ(scene.findObjectById(second->id()), second);
    EXPECT_EQ(scene.findObjectById(first->id()), first);
}

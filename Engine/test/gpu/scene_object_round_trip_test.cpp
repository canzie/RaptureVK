#include "registered_assets_gpu_test.h"

#include <rapture/assets/asset_manager/AssetManager.h>
#include <rapture/assets/asset_manager/ReservedAssets.h>
#include <rapture/core/serialization/SerialDocument.h>
#include <rapture/physics/Common.h>
#include <rapture/scene/Scene.h>
#include <rapture/scene/components/Components.h>
#include <rapture/scene/instances/Bone3D.h>
#include <rapture/scene/instances/Box3D.h>
#include <rapture/scene/instances/Camera3D.h>
#include <rapture/scene/instances/CharacterBody3D.h>
#include <rapture/scene/instances/DirectionalLight3D.h>
#include <rapture/scene/instances/Environment.h>
#include <rapture/scene/instances/Folder.h>
#include <rapture/scene/instances/Module.h>
#include <rapture/scene/instances/Node3D.h>
#include <rapture/scene/instances/Plane3D.h>
#include <rapture/scene/instances/PointLight3D.h>
#include <rapture/scene/instances/RigidBody3D.h>
#include <rapture/scene/instances/SkeletalMesh3D.h>
#include <rapture/scene/instances/SkeletonPose.h>
#include <rapture/scene/instances/Sphere3D.h>
#include <rapture/scene/instances/SpotLight3D.h>
#include <rapture/scene/instances/SpringArm3D.h>
#include <rapture/scene/instances/StaticMesh3D.h>
#include <rapture/scene/instances/Terrain3D.h>
#include <rapture/scene/instances/controllers/CameraController.h>
#include <rapture/scene/instances/controllers/PlayerController.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <functional>
#include <memory>
#include <variant>

static constexpr float VALUE_TOLERANCE = 1e-5f;

static void s_expectVec3Near(const glm::vec3 &actual, const glm::vec3 &expected)
{
    EXPECT_NEAR(actual.x, expected.x, VALUE_TOLERANCE);
    EXPECT_NEAR(actual.y, expected.y, VALUE_TOLERANCE);
    EXPECT_NEAR(actual.z, expected.z, VALUE_TOLERANCE);
}

static void s_expectQuatNear(const glm::quat &actual, const glm::quat &expected)
{
    // q and -q are the same rotation
    EXPECT_NEAR(std::abs(glm::dot(actual, expected)), 1.0f, VALUE_TOLERANCE);
}

/**
 * @brief Each scene object class keeps every field the editor can change through a restore into the live object
 * and through a load into a fresh scene
 */
class SceneObjectRoundTrip : public RegisteredAssetsGpuTest {
  protected:
    /**
     * @brief Snapshots the scene, changes an object away from what it was, then checks it comes back on restore and on load
     * @param scene The scene holding the object
     * @param object The object, already set to the values the snapshot should hold
     * @param changeAway Sets the object to values other than the snapshot's
     * @param expectSnapshotValues Checks an object holds the snapshot's values
     */
    template <typename T>
    void expectRestoredAndLoaded(Rapture::Scene &scene, T &object, const std::function<void(T &)> &changeAway,
                                 const std::function<void(const T &)> &expectSnapshotValues)
    {
        const Rapture::InstanceId id = object.id();
        Rapture::SerialDocument snapshot = scene.snapshot();

        changeAway(object);
        ASSERT_TRUE(scene.restoreFrom(snapshot.rootView()));

        {
            SCOPED_TRACE("restored into the live object");
            expectSnapshotValues(object);
        }

        std::unique_ptr<Rapture::Scene> loaded = Rapture::Scene::deserialize(snapshot.rootView());
        ASSERT_NE(loaded, nullptr);

        Rapture::SceneObject *found = loaded->findObjectById(id);
        ASSERT_NE(found, nullptr);
        T *typed = found->as<T>();
        ASSERT_NE(typed, nullptr);

        {
            SCOPED_TRACE("loaded into a fresh scene");
            expectSnapshotValues(*typed);
        }
    }
};

TEST_F(SceneObjectRoundTrip, Node3D)
{
    Rapture::Scene scene("SceneObjectRoundTrip");
    auto *node = scene.root()->add<Rapture::Node3D>("Node");

    const glm::quat rotation = glm::angleAxis(0.7f, glm::normalize(glm::vec3(1.0f, 2.0f, 3.0f)));
    node->setPosition(glm::vec3(1.0f, -2.0f, 3.5f));
    node->setRotation(rotation);
    node->setScale(glm::vec3(2.0f, 0.5f, 3.0f));
    node->visibility().inOutliner = false;
    node->attributes().setFloat("speed", 2.5f);

    expectRestoredAndLoaded<Rapture::Node3D>(
        scene, *node,
        [](Rapture::Node3D &n) {
            n.setName("Renamed");
            n.setPosition(glm::vec3(0.0f));
            n.setRotation(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
            n.setScale(glm::vec3(1.0f));
            n.visibility().inOutliner = true;
            n.attributes().setFloat("speed", 1.0f);
        },
        [&rotation](const Rapture::Node3D &n) {
            EXPECT_EQ(n.name(), "Node");
            s_expectVec3Near(n.position(), glm::vec3(1.0f, -2.0f, 3.5f));
            s_expectQuatNear(n.rotationQuat(), rotation);
            s_expectVec3Near(n.scale(), glm::vec3(2.0f, 0.5f, 3.0f));
            EXPECT_FALSE(n.visibility().inOutliner);
            EXPECT_FLOAT_EQ(n.attributes().getFloat("speed"), 2.5f);
        });
}

TEST_F(SceneObjectRoundTrip, Folder)
{
    Rapture::Scene scene("SceneObjectRoundTrip");
    auto *folder = scene.root()->add<Rapture::Folder>("Folder");
    folder->add<Rapture::Node3D>("Inside");

    expectRestoredAndLoaded<Rapture::Folder>(
        scene, *folder,
        [](Rapture::Folder &f) {
            f.setName("Renamed");
            f.add<Rapture::Node3D>("Gained");
        },
        [](const Rapture::Folder &f) {
            EXPECT_EQ(f.name(), "Folder");
            ASSERT_EQ(f.children().size(), 1u);
            EXPECT_EQ(f.children()[0]->name(), "Inside");
        });
}

TEST_F(SceneObjectRoundTrip, Camera3D)
{
    Rapture::Scene scene("SceneObjectRoundTrip");
    auto *camera = scene.root()->add<Rapture::Camera3D>("Camera");
    camera->setFieldOfView(75.0f);
    camera->setNearPlane(0.5f);
    camera->setFarPlane(500.0f);
    camera->setExposureStops(-1.5f);

    expectRestoredAndLoaded<Rapture::Camera3D>(
        scene, *camera,
        [](Rapture::Camera3D &c) {
            c.setFieldOfView(45.0f);
            c.setNearPlane(0.1f);
            c.setFarPlane(100.0f);
            c.setExposureStops(2.0f);
        },
        [](const Rapture::Camera3D &c) {
            EXPECT_FLOAT_EQ(c.fieldOfView(), 75.0f);
            EXPECT_FLOAT_EQ(c.nearPlane(), 0.5f);
            EXPECT_FLOAT_EQ(c.farPlane(), 500.0f);
            EXPECT_FLOAT_EQ(c.exposureStops(), -1.5f);
        });
}

TEST_F(SceneObjectRoundTrip, SpringArm3D)
{
    Rapture::Scene scene("SceneObjectRoundTrip");
    auto *arm = scene.root()->add<Rapture::SpringArm3D>("Arm");
    arm->setLength(7.5f);
    arm->setFollowsControlRotation(true);

    expectRestoredAndLoaded<Rapture::SpringArm3D>(
        scene, *arm,
        [](Rapture::SpringArm3D &a) {
            a.setLength(2.0f);
            a.setFollowsControlRotation(false);
        },
        [](const Rapture::SpringArm3D &a) {
            EXPECT_FLOAT_EQ(a.length(), 7.5f);
            EXPECT_TRUE(a.followsControlRotation());
        });
}

TEST_F(SceneObjectRoundTrip, StaticMesh3D)
{
    Rapture::Scene scene("SceneObjectRoundTrip");
    auto *mesh = scene.root()->add<Rapture::StaticMesh3D>("Mesh");
    mesh->setMesh(Rapture::RE_PRIMITIVE_SPHERE_MESH);
    mesh->setMaterial(0, Rapture::RE_GRID_MATERIAL_INSTANCE);
    mesh->setVisible(false);
    mesh->setMobility(Rapture::MOBILITY_DYNAMIC);

    expectRestoredAndLoaded<Rapture::StaticMesh3D>(
        scene, *mesh,
        [](Rapture::StaticMesh3D &m) {
            m.setMaterial(0, Rapture::RE_DEFAULT_MATERIAL_INSTANCE);
            m.setMesh(Rapture::RE_PRIMITIVE_CUBE_MESH);
            m.setVisible(true);
            m.setMobility(Rapture::MOBILITY_STATIC);
        },
        [](const Rapture::StaticMesh3D &m) {
            EXPECT_EQ(m.mesh(), static_cast<Rapture::AssetHandle>(Rapture::RE_PRIMITIVE_SPHERE_MESH));
            ASSERT_EQ(m.materials().size(), 1u);
            EXPECT_EQ(m.material(0), static_cast<Rapture::AssetHandle>(Rapture::RE_GRID_MATERIAL_INSTANCE));
            EXPECT_FALSE(m.isVisible());
            EXPECT_EQ(m.mobility(), Rapture::MOBILITY_DYNAMIC);
        });
}

TEST_F(SceneObjectRoundTrip, StaticMesh3DGoesBackToNoMesh)
{
    Rapture::Scene scene("SceneObjectRoundTrip");
    auto *mesh = scene.root()->add<Rapture::StaticMesh3D>("Mesh");

    expectRestoredAndLoaded<Rapture::StaticMesh3D>(
        scene, *mesh, [](Rapture::StaticMesh3D &m) { m.setMesh(Rapture::RE_PRIMITIVE_CUBE_MESH); },
        [](const Rapture::StaticMesh3D &m) {
            EXPECT_EQ(m.mesh(), Rapture::INVALID_ASSET_HANDLE);
            EXPECT_TRUE(m.materials().empty());
            EXPECT_FALSE(m.isRayTraced());
        });
}

TEST_F(SceneObjectRoundTrip, MaterialSlotGoesBackToNoMaterial)
{
    Rapture::Scene scene("SceneObjectRoundTrip");
    auto *mesh = scene.root()->add<Rapture::StaticMesh3D>("Mesh");
    mesh->setMesh(Rapture::RE_PRIMITIVE_CUBE_MESH);
    mesh->setMaterial(0, Rapture::INVALID_ASSET_HANDLE);

    expectRestoredAndLoaded<Rapture::StaticMesh3D>(
        scene, *mesh, [](Rapture::StaticMesh3D &m) { m.setMaterial(0, Rapture::RE_DEFAULT_MATERIAL_INSTANCE); },
        [](const Rapture::StaticMesh3D &m) {
            ASSERT_EQ(m.materials().size(), 1u);
            EXPECT_EQ(m.material(0), Rapture::INVALID_ASSET_HANDLE);
        });
}

TEST_F(SceneObjectRoundTrip, Box3D)
{
    Rapture::Scene scene("SceneObjectRoundTrip");
    auto *box = scene.root()->add<Rapture::Box3D>("Box");
    box->setMaterial(0, Rapture::RE_GRID_MATERIAL_INSTANCE);
    box->setScale(glm::vec3(2.0f, 1.0f, 3.0f));

    expectRestoredAndLoaded<Rapture::Box3D>(
        scene, *box,
        [](Rapture::Box3D &b) {
            b.setMaterial(0, Rapture::RE_DEFAULT_MATERIAL_INSTANCE);
            b.setScale(glm::vec3(1.0f));
        },
        [](const Rapture::Box3D &b) {
            EXPECT_EQ(b.mesh(), static_cast<Rapture::AssetHandle>(Rapture::RE_PRIMITIVE_CUBE_MESH));
            EXPECT_EQ(b.material(0), static_cast<Rapture::AssetHandle>(Rapture::RE_GRID_MATERIAL_INSTANCE));
            s_expectVec3Near(b.scale(), glm::vec3(2.0f, 1.0f, 3.0f));
        });
}

TEST_F(SceneObjectRoundTrip, Sphere3D)
{
    Rapture::Scene scene("SceneObjectRoundTrip");
    auto *sphere = scene.root()->add<Rapture::Sphere3D>("Sphere");
    sphere->setVisible(false);

    expectRestoredAndLoaded<Rapture::Sphere3D>(
        scene, *sphere, [](Rapture::Sphere3D &s) { s.setVisible(true); },
        [](const Rapture::Sphere3D &s) {
            EXPECT_EQ(s.mesh(), static_cast<Rapture::AssetHandle>(Rapture::RE_PRIMITIVE_SPHERE_MESH));
            EXPECT_EQ(s.material(0), static_cast<Rapture::AssetHandle>(Rapture::RE_DEFAULT_MATERIAL_INSTANCE));
            EXPECT_FALSE(s.isVisible());
        });
}

TEST_F(SceneObjectRoundTrip, Plane3D)
{
    Rapture::Scene scene("SceneObjectRoundTrip");
    auto *plane = scene.root()->add<Rapture::Plane3D>("Plane");
    plane->setMobility(Rapture::MOBILITY_DYNAMIC);

    expectRestoredAndLoaded<Rapture::Plane3D>(
        scene, *plane, [](Rapture::Plane3D &p) { p.setMobility(Rapture::MOBILITY_STATIC); },
        [](const Rapture::Plane3D &p) {
            EXPECT_EQ(p.mesh(), static_cast<Rapture::AssetHandle>(Rapture::RE_PRIMITIVE_PLANE_MESH));
            EXPECT_EQ(p.mobility(), Rapture::MOBILITY_DYNAMIC);
        });
}

TEST_F(SceneObjectRoundTrip, SkeletonPose)
{
    const Rapture::AssetHandle skeleton = registerTwoJointSkeleton("SceneObjectRoundTrip.SkeletonPose");

    Rapture::Scene scene("SceneObjectRoundTrip");
    auto *pose = addPose(*scene.root(), skeleton);
    ASSERT_NE(pose, nullptr);
    pose->setPosition(glm::vec3(0.0f, 2.0f, 0.0f));

    expectRestoredAndLoaded<Rapture::SkeletonPose>(
        scene, *pose, [](Rapture::SkeletonPose &p) { p.setPosition(glm::vec3(0.0f)); },
        [skeleton](const Rapture::SkeletonPose &p) {
            EXPECT_EQ(p.skeleton(), skeleton);
            EXPECT_EQ(p.getJointCount(), 2u);
            ASSERT_NE(p.getBone(1), nullptr);
            EXPECT_EQ(p.getBone(1)->name(), "Tip");
            s_expectVec3Near(p.position(), glm::vec3(0.0f, 2.0f, 0.0f));
        });
}

TEST_F(SceneObjectRoundTrip, SkeletalMesh3D)
{
    const Rapture::AssetHandle skeleton = registerTwoJointSkeleton("SceneObjectRoundTrip.SkeletalMesh3D");

    Rapture::Scene scene("SceneObjectRoundTrip");
    auto *pose = addPose(*scene.root(), skeleton);
    ASSERT_NE(pose, nullptr);
    auto *mesh = scene.root()->add<Rapture::SkeletalMesh3D>("Mesh");
    mesh->setPose(pose);
    mesh->setVisible(false);

    const Rapture::InstanceId poseId = pose->id();

    expectRestoredAndLoaded<Rapture::SkeletalMesh3D>(
        scene, *mesh,
        [](Rapture::SkeletalMesh3D &m) {
            m.setPose(nullptr);
            m.setVisible(true);
        },
        [poseId](const Rapture::SkeletalMesh3D &m) {
            ASSERT_NE(m.pose(), nullptr);
            EXPECT_EQ(m.pose()->id(), poseId);
            EXPECT_FALSE(m.isVisible());
        });
}

TEST_F(SceneObjectRoundTrip, Terrain3D)
{
    static constexpr uint32_t GRID_SIDE = 3;
    static constexpr float CHUNK_SIZE = 32.0f;

    Rapture::TerrainConfig authored;
    authored.chunkWorldSize = CHUNK_SIZE;
    authored.terrainWorldSize = CHUNK_SIZE * GRID_SIDE;
    authored.chunkGridSize = GRID_SIDE * GRID_SIDE;
    authored.heightScale = 40.0f;
    authored.hmType = Rapture::HM_SINGLE;

    Rapture::Scene scene("SceneObjectRoundTrip");
    auto *terrain = scene.root()->add<Rapture::Terrain3D>("Terrain");
    terrain->setConfig(authored);
    terrain->setEnabled(false);

    expectRestoredAndLoaded<Rapture::Terrain3D>(
        scene, *terrain,
        [&authored](Rapture::Terrain3D &t) {
            Rapture::TerrainConfig other = authored;
            other.heightScale = 10.0f;
            other.hmType = Rapture::HM_CEPV;
            t.setConfig(other);
            t.setEnabled(true);
        },
        [&authored](const Rapture::Terrain3D &t) {
            EXPECT_TRUE(t.config() == authored);
            EXPECT_FALSE(t.isEnabled());
        });
}

TEST_F(SceneObjectRoundTrip, PointLight3D)
{
    Rapture::Scene scene("SceneObjectRoundTrip");
    auto *light = scene.root()->add<Rapture::PointLight3D>("Light");
    light->setColor(glm::vec3(1.0f, 0.5f, 0.25f));
    light->setUsesTemperature(true);
    light->setTemperature(3200.0f);
    light->setIntensity(12.0f);
    light->setActive(false);
    light->setMobility(Rapture::MOBILITY_DYNAMIC);
    light->setRange(25.0f);
    light->setCastsShadow(true);
    light->accessor().write<Rapture::ShadowComponent>()->isActive = false;

    expectRestoredAndLoaded<Rapture::PointLight3D>(
        scene, *light,
        [](Rapture::PointLight3D &l) {
            l.setColor(glm::vec3(1.0f));
            l.setUsesTemperature(false);
            l.setTemperature(6500.0f);
            l.setIntensity(1.0f);
            l.setActive(true);
            l.setMobility(Rapture::MOBILITY_STATIC);
            l.setRange(5.0f);
            l.setCastsShadow(false);
        },
        [](const Rapture::PointLight3D &l) {
            s_expectVec3Near(l.color(), glm::vec3(1.0f, 0.5f, 0.25f));
            EXPECT_TRUE(l.usesTemperature());
            EXPECT_FLOAT_EQ(l.temperature(), 3200.0f);
            EXPECT_FLOAT_EQ(l.intensity(), 12.0f);
            EXPECT_FALSE(l.isActive());
            EXPECT_EQ(l.mobility(), Rapture::MOBILITY_DYNAMIC);
            EXPECT_FLOAT_EQ(l.range(), 25.0f);
            EXPECT_TRUE(l.castsShadow());

            const Rapture::ShadowComponent *shadow = l.accessor().tryRead<Rapture::ShadowComponent>();
            ASSERT_NE(shadow, nullptr);
            EXPECT_FALSE(shadow->isActive);
        });
}

TEST_F(SceneObjectRoundTrip, SpotLight3D)
{
    Rapture::Scene scene("SceneObjectRoundTrip");
    auto *light = scene.root()->add<Rapture::SpotLight3D>("Light");
    light->setRange(30.0f);
    light->setInnerConeAngle(0.2f);
    light->setOuterConeAngle(0.5f);
    light->setIntensity(8.0f);

    expectRestoredAndLoaded<Rapture::SpotLight3D>(
        scene, *light,
        [](Rapture::SpotLight3D &l) {
            l.setRange(10.0f);
            l.setInnerConeAngle(0.1f);
            l.setOuterConeAngle(0.3f);
            l.setIntensity(1.0f);
        },
        [](const Rapture::SpotLight3D &l) {
            EXPECT_FLOAT_EQ(l.range(), 30.0f);
            EXPECT_FLOAT_EQ(l.innerConeAngle(), 0.2f);
            EXPECT_FLOAT_EQ(l.outerConeAngle(), 0.5f);
            EXPECT_FLOAT_EQ(l.intensity(), 8.0f);
        });
}

TEST_F(SceneObjectRoundTrip, DirectionalLight3D)
{
    Rapture::Scene scene("SceneObjectRoundTrip");
    auto *light = scene.root()->add<Rapture::DirectionalLight3D>("Sun");
    light->setAtmosphereSun(true);
    light->setCastsShadow(true);
    light->setShadowDistance(150.0f);
    {
        auto shadow = light->accessor().write<Rapture::CascadedShadowComponent>();
        shadow->lambda = 0.5f;
        shadow->isActive = false;
    }

    expectRestoredAndLoaded<Rapture::DirectionalLight3D>(
        scene, *light,
        [](Rapture::DirectionalLight3D &l) {
            l.setAtmosphereSun(false);
            l.setShadowDistance(40.0f);
            l.accessor().write<Rapture::CascadedShadowComponent>()->lambda = 0.9f;
            l.setCastsShadow(false);
        },
        [](const Rapture::DirectionalLight3D &l) {
            EXPECT_TRUE(l.isAtmosphereSun());
            EXPECT_TRUE(l.castsShadow());

            const Rapture::CascadedShadowComponent *shadow = l.accessor().tryRead<Rapture::CascadedShadowComponent>();
            ASSERT_NE(shadow, nullptr);
            EXPECT_FLOAT_EQ(shadow->shadowDistance, 150.0f);
            EXPECT_FLOAT_EQ(shadow->lambda, 0.5f);
            EXPECT_FALSE(shadow->isActive);
        });
}

TEST_F(SceneObjectRoundTrip, Environment)
{
    Rapture::Scene scene("SceneObjectRoundTrip");
    Rapture::Environment *environment = scene.environment();
    ASSERT_NE(environment, nullptr);

    environment->setSkyIntensity(2.5f);
    environment->setSkyboxEnabled(false);
    environment->setUsesAtmosphereSkybox(true);
    environment->atmosphere().timeOfDay = 18.0f;
    environment->atmosphere().latitude = 52.0f;
    environment->atmosphere().longitude = 4.5f;
    environment->atmosphere().mie = 30.0f;
    environment->atmosphere().mieG = 0.8f;
    environment->atmosphere().sunIntensity = 15.0f;
    environment->atmosphere().cameraAltitude = 2.0f;
    environment->atmosphere().rayleigh = glm::vec3(6.0f, 14.0f, 30.0f);

    expectRestoredAndLoaded<Rapture::Environment>(
        scene, *environment,
        [](Rapture::Environment &e) {
            e.setSkyIntensity(1.0f);
            e.setSkyboxEnabled(true);
            e.atmosphere().timeOfDay = 12.0f;
            e.atmosphere().latitude = 0.0f;
            e.atmosphere().longitude = 0.0f;
            e.atmosphere().mie = 21.0f;
            e.atmosphere().mieG = 0.76f;
            e.atmosphere().sunIntensity = 20.0f;
            e.atmosphere().cameraAltitude = 1.0f;
            e.atmosphere().rayleigh = glm::vec3(5.8f, 13.5f, 33.1f);
        },
        [](const Rapture::Environment &e) {
            EXPECT_FLOAT_EQ(e.skyIntensity(), 2.5f);
            EXPECT_FALSE(e.isSkyboxEnabled());
            EXPECT_TRUE(e.usesAtmosphereSkybox());
            EXPECT_FLOAT_EQ(e.atmosphere().timeOfDay, 18.0f);
            EXPECT_FLOAT_EQ(e.atmosphere().latitude, 52.0f);
            EXPECT_FLOAT_EQ(e.atmosphere().longitude, 4.5f);
            EXPECT_FLOAT_EQ(e.atmosphere().mie, 30.0f);
            EXPECT_FLOAT_EQ(e.atmosphere().mieG, 0.8f);
            EXPECT_FLOAT_EQ(e.atmosphere().sunIntensity, 15.0f);
            EXPECT_FLOAT_EQ(e.atmosphere().cameraAltitude, 2.0f);
            s_expectVec3Near(e.atmosphere().rayleigh, glm::vec3(6.0f, 14.0f, 30.0f));
        });
}

TEST_F(SceneObjectRoundTrip, Module)
{
    const Rapture::AssetHandle module = registerSingleNodeModule("SceneObjectRoundTrip.Module");

    Rapture::Scene scene("SceneObjectRoundTrip");
    auto *placed = scene.root()->add<Rapture::Module>("Placed");
    ASSERT_TRUE(placed->setAssetHandle(module));

    expectRestoredAndLoaded<Rapture::Module>(
        scene, *placed, [](Rapture::Module &m) { m.setAssetHandle(Rapture::INVALID_ASSET_HANDLE); },
        [module](const Rapture::Module &m) {
            EXPECT_EQ(m.assetHandle(), module);
            EXPECT_NE(m.findChild("Content", true), nullptr);
        });
}

TEST_F(SceneObjectRoundTrip, ModuleGoesBackToNoAsset)
{
    const Rapture::AssetHandle module = registerSingleNodeModule("SceneObjectRoundTrip.ModuleGoesBackToNoAsset");

    Rapture::Scene scene("SceneObjectRoundTrip");
    auto *placed = scene.root()->add<Rapture::Module>("Placed");

    expectRestoredAndLoaded<Rapture::Module>(
        scene, *placed, [module](Rapture::Module &m) { m.setAssetHandle(module); },
        [](const Rapture::Module &m) {
            EXPECT_EQ(m.assetHandle(), Rapture::INVALID_ASSET_HANDLE);
            EXPECT_EQ(m.findChild("Content", true), nullptr);
        });
}

TEST_F(SceneObjectRoundTrip, CameraController)
{
    Rapture::Scene scene("SceneObjectRoundTrip");
    auto *controller = scene.root()->add<Rapture::CameraController>("Controller");
    controller->mouseSensitivity = 0.4f;
    controller->movementSpeed = 12.0f;
    controller->orbitSensitivity = 0.6f;
    controller->panSpeed = 0.01f;
    controller->zoomSpeed = 0.5f;
    controller->setMaxPitch(60.0f);

    expectRestoredAndLoaded<Rapture::CameraController>(
        scene, *controller,
        [](Rapture::CameraController &c) {
            c.mouseSensitivity = 0.1f;
            c.movementSpeed = 5.0f;
            c.orbitSensitivity = 0.3f;
            c.panSpeed = 0.0015f;
            c.zoomSpeed = 0.15f;
            c.setMaxPitch(89.0f);
        },
        [](const Rapture::CameraController &c) {
            EXPECT_FLOAT_EQ(c.mouseSensitivity, 0.4f);
            EXPECT_FLOAT_EQ(c.movementSpeed, 12.0f);
            EXPECT_FLOAT_EQ(c.orbitSensitivity, 0.6f);
            EXPECT_FLOAT_EQ(c.panSpeed, 0.01f);
            EXPECT_FLOAT_EQ(c.zoomSpeed, 0.5f);
            EXPECT_FLOAT_EQ(c.maxPitch(), 60.0f);
        });
}

TEST_F(SceneObjectRoundTrip, PlayerController)
{
    Rapture::Scene scene("SceneObjectRoundTrip");
    auto *controller = scene.root()->add<Rapture::PlayerController>("Controller");
    controller->movementSpeed = 9.0f;
    controller->mouseSensitivity = 0.25f;
    controller->setMaxPitch(70.0f);

    expectRestoredAndLoaded<Rapture::PlayerController>(
        scene, *controller,
        [](Rapture::PlayerController &c) {
            c.movementSpeed = 5.0f;
            c.mouseSensitivity = 0.1f;
            c.setMaxPitch(89.0f);
        },
        [](const Rapture::PlayerController &c) {
            EXPECT_FLOAT_EQ(c.movementSpeed, 9.0f);
            EXPECT_FLOAT_EQ(c.mouseSensitivity, 0.25f);
            EXPECT_FLOAT_EQ(c.maxPitch(), 70.0f);
        });
}

TEST_F(SceneObjectRoundTrip, RigidBody3D)
{
    const glm::mat4 localTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.5f, 0.0f));

    Rapture::Scene scene("SceneObjectRoundTrip");
    auto *host = scene.root()->add<Rapture::Node3D>("Host");
    auto *body = host->addComponent<Rapture::RigidBody3D>();
    ASSERT_NE(body, nullptr);
    body->setShape(Rapture::physics::BoxShape{glm::vec3(1.0f, 2.0f, 3.0f)});
    body->setLocalTransform(localTransform);
    body->setMotionType(Rapture::physics::MOTION_KINEMATIC);
    body->setFriction(0.7f);
    body->setRestitution(0.3f);

    expectRestoredAndLoaded<Rapture::Node3D>(
        scene, *host,
        [](Rapture::Node3D &h) {
            auto *b = h.component<Rapture::RigidBody3D>();
            b->setShape(Rapture::physics::SphereShape{1.0f});
            b->setLocalTransform(glm::mat4(1.0f));
            b->setMotionType(Rapture::physics::MOTION_DYNAMIC);
            b->setFriction(0.2f);
            b->setRestitution(0.0f);
        },
        [&localTransform](const Rapture::Node3D &h) {
            const auto *b = h.component<Rapture::RigidBody3D>();
            ASSERT_NE(b, nullptr);

            const auto *box = std::get_if<Rapture::physics::BoxShape>(&b->shape());
            ASSERT_NE(box, nullptr);
            s_expectVec3Near(box->halfExtents, glm::vec3(1.0f, 2.0f, 3.0f));
            s_expectVec3Near(glm::vec3(b->localTransform()[3]), glm::vec3(localTransform[3]));
            EXPECT_EQ(b->motionType(), Rapture::physics::MOTION_KINEMATIC);
            EXPECT_FLOAT_EQ(b->friction(), 0.7f);
            EXPECT_FLOAT_EQ(b->restitution(), 0.3f);
        });
}

TEST_F(SceneObjectRoundTrip, CharacterBody3D)
{
    Rapture::Scene scene("SceneObjectRoundTrip");
    auto *host = scene.root()->add<Rapture::Node3D>("Host");
    auto *body = host->addComponent<Rapture::CharacterBody3D>();
    ASSERT_NE(body, nullptr);
    body->setShape(Rapture::physics::CapsuleShape{0.8f, 0.3f});
    body->setShapeOffset(glm::vec3(0.0f, 1.1f, 0.0f));
    body->setMass(90.0f);
    body->setMaxSlopeAngle(0.6f);
    body->setStepUp(0.3f);
    body->setStepDown(0.7f);
    body->setJumpSpeed(6.0f);

    expectRestoredAndLoaded<Rapture::Node3D>(
        scene, *host,
        [](Rapture::Node3D &h) {
            auto *b = h.component<Rapture::CharacterBody3D>();
            b->setShape(Rapture::physics::SphereShape{0.5f});
            b->setShapeOffset(glm::vec3(0.0f));
            b->setMass(70.0f);
            b->setMaxSlopeAngle(0.9f);
            b->setStepUp(0.4f);
            b->setStepDown(0.5f);
            b->setJumpSpeed(4.0f);
        },
        [](const Rapture::Node3D &h) {
            const auto *b = h.component<Rapture::CharacterBody3D>();
            ASSERT_NE(b, nullptr);

            const auto *capsule = std::get_if<Rapture::physics::CapsuleShape>(&b->shape());
            ASSERT_NE(capsule, nullptr);
            EXPECT_FLOAT_EQ(capsule->halfHeight, 0.8f);
            EXPECT_FLOAT_EQ(capsule->radius, 0.3f);
            s_expectVec3Near(b->shapeOffset(), glm::vec3(0.0f, 1.1f, 0.0f));
            EXPECT_FLOAT_EQ(b->mass(), 90.0f);
            EXPECT_FLOAT_EQ(b->maxSlopeAngle(), 0.6f);
            EXPECT_FLOAT_EQ(b->stepUp(), 0.3f);
            EXPECT_FLOAT_EQ(b->stepDown(), 0.7f);
            EXPECT_FLOAT_EQ(b->jumpSpeed(), 6.0f);
        });
}

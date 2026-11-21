#ifndef RAPTURE__REGISTERED_ASSETS_GPU_TEST_H
#define RAPTURE__REGISTERED_ASSETS_GPU_TEST_H

#include "scene_document_helpers.h"
#include "windowless_application_environment.h"

#include <rapture/assets/asset_manager/AssetManager.h>
#include <rapture/assets/modules/AModule.h>
#include <rapture/assets/skeletons/ASkeleton.h>
#include <rapture/assets/skeletons/Skeleton.h>
#include <rapture/scene/Scene.h>
#include <rapture/scene/instances/Node3D.h>
#include <rapture/scene/instances/SkeletonPose.h>

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

/**
 * @brief A test on the windowless application that registers assets of its own, unregistered again when it ends
 */
class RegisteredAssetsGpuTest : public WindowlessGpuTest {
  protected:
    void TearDown() override
    {
        std::vector<Rapture::AssetHandle> handles;
        for (const Rapture::AssetRef &asset : m_registeredAssets) {
            handles.push_back(asset->handle());
        }

        // unregistering frees the asset, so no ref may still point at it
        m_registeredAssets.clear();

        for (Rapture::AssetHandle handle : handles) {
            Rapture::AssetManager::unregisterVirtualAsset(handle);
        }

        WindowlessGpuTest::TearDown();
    }

    /**
     * @brief Registers a skeleton of a root joint named Root with one child named Tip
     * @param name The name it is registered under
     * @return The skeleton's handle
     */
    Rapture::AssetHandle registerTwoJointSkeleton(std::string_view name)
    {
        auto skeleton = std::make_unique<Rapture::Skeleton>();

        Rapture::Skeleton::JointTransform rest;
        const Rapture::Skeleton::JointIndex root = skeleton->addJoint("Root", Rapture::Skeleton::INVALID_JOINT_INDEX, rest);
        rest.position = glm::vec3(0.0f, 1.0f, 0.0f);
        skeleton->addJoint("Tip", root, rest);

        return registerAsset(std::make_unique<Rapture::ASkeleton>(std::move(skeleton)), name, Rapture::ASSET_SKELETON);
    }

    /**
     * @brief Registers a module holding one Node3D named Content
     * @param name The name it is registered under
     * @return The module's handle
     */
    Rapture::AssetHandle registerSingleNodeModule(std::string_view name)
    {
        Rapture::Scene source("ModuleSource");
        auto *node = source.root()->add<Rapture::Node3D>("Content");

        return registerAsset(std::make_unique<Rapture::AModule>(SceneDocument_writeSubtree(*node)), name, Rapture::ASSET_MODULE);
    }

    /**
     * @brief Adds a pose of a skeleton under a parent
     * @param parent The scene object the pose goes under
     * @param skeleton The skeleton it poses
     * @return The pose, or nullptr if it could not be added
     */
    static Rapture::SkeletonPose *addPose(Rapture::SceneObject &parent, Rapture::AssetHandle skeleton)
    {
        Rapture::SceneObject *added = parent.addChild(std::make_unique<Rapture::SkeletonPose>(*parent.scene(), "Pose", skeleton));
        return added != nullptr ? added->as<Rapture::SkeletonPose>() : nullptr;
    }

  private:
    Rapture::AssetHandle registerAsset(std::unique_ptr<Rapture::Asset> asset, std::string_view name, Rapture::AssetType type)
    {
        Rapture::AssetRef ref = Rapture::AssetManager::registerVirtualAsset(std::move(asset), std::string(name), type);
        if (!ref) {
            ADD_FAILURE() << "could not register " << name;
            return Rapture::INVALID_ASSET_HANDLE;
        }

        m_registeredAssets.push_back(ref);
        return ref->handle();
    }

  private:
    std::vector<Rapture::AssetRef> m_registeredAssets;
};

#endif // RAPTURE__REGISTERED_ASSETS_GPU_TEST_H

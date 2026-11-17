#include "windowless_application_environment.h"

#include <rapture/scene/Scene.h>
#include <rapture/scene/instances/Node3D.h>

#include <memory>
#include <string_view>
#include <vector>

static constexpr float FRAME_DELTA_TIME = 1.0f / 60.0f;

class DestroyingNode : public Rapture::Node3D {
  public:
    DestroyingNode(Rapture::Scene &scene, std::string_view name) : Node3D(scene, name) {}

    void onUpdate(float dt) override
    {
        (void)dt;
        updateCount++;
        for (Rapture::SceneObject *target : targets) {
            scene()->destroyInstance(target);
        }
        aliveAfterRequests = *aliveFlag();
    }

    std::vector<Rapture::SceneObject *> targets;
    int updateCount = 0;
    bool aliveAfterRequests = false;
};

class SceneDestroy : public WindowlessGpuTest {};

TEST_F(SceneDestroy, DestroyingItselfDuringTickWaitsForThePhaseToEnd)
{
    Rapture::Scene scene("SceneDestroy");

    auto *node = scene.root()->add<DestroyingNode>("Self");
    node->targets.push_back(node);
    node->setTickPhase(Rapture::TICK_PRE_PHYSICS);
    node->setTickEnabled(true);
    std::shared_ptr<bool> alive = node->aliveFlag();

    scene.runTickPhase(Rapture::TICK_PRE_PHYSICS, FRAME_DELTA_TIME);

    EXPECT_EQ(node->updateCount, 1);
    EXPECT_TRUE(node->aliveAfterRequests);
    EXPECT_FALSE(*alive);
}

TEST_F(SceneDestroy, DestroyingParentAndItselfDuringTickDestroysBothOnce)
{
    Rapture::Scene scene("SceneDestroy");

    auto *parent = scene.root()->add<Rapture::Node3D>("Parent");
    auto *child = parent->add<DestroyingNode>("Child");
    child->targets = {parent, child};
    child->setTickPhase(Rapture::TICK_PRE_PHYSICS);
    child->setTickEnabled(true);
    std::shared_ptr<bool> parentAlive = parent->aliveFlag();
    std::shared_ptr<bool> childAlive = child->aliveFlag();

    scene.runTickPhase(Rapture::TICK_PRE_PHYSICS, FRAME_DELTA_TIME);

    EXPECT_TRUE(child->aliveAfterRequests);
    EXPECT_FALSE(*parentAlive);
    EXPECT_FALSE(*childAlive);
}

TEST_F(SceneDestroy, DestroyingOutsideTickIsImmediate)
{
    Rapture::Scene scene("SceneDestroy");

    auto *node = scene.root()->add<Rapture::Node3D>("Node");
    std::shared_ptr<bool> alive = node->aliveFlag();

    scene.destroyInstance(node);

    EXPECT_FALSE(*alive);
}

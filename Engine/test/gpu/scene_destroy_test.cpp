#include "test_flag_component.h"
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

TEST_F(SceneDestroy, OnDestroyFiresOncePerInstance)
{
    Rapture::Scene scene("SceneDestroy");

    auto *parent = scene.root()->add<Rapture::Node3D>("Parent");
    auto *child = parent->add<Rapture::Node3D>("Child");
    auto *component = child->addComponent<TestFlagComponent>();

    int parentFires = 0;
    int childFires = 0;
    int componentFires = 0;
    std::vector<Rapture::EventConnection> connections;
    connections.push_back(parent->onDestroy.connect([&parentFires](Rapture::Instance *) { parentFires++; }));
    connections.push_back(child->onDestroy.connect([&childFires](Rapture::Instance *) { childFires++; }));
    connections.push_back(component->onDestroy.connect([&componentFires](Rapture::Instance *) { componentFires++; }));

    scene.destroyInstance(parent);

    EXPECT_EQ(parentFires, 1);
    EXPECT_EQ(childFires, 1);
    EXPECT_EQ(componentFires, 1);
}

TEST_F(SceneDestroy, ListenersFindTheDyingSubtreeIntactAndUnchangeable)
{
    Rapture::Scene scene("SceneDestroy");

    auto *parent = scene.root()->add<Rapture::Node3D>("Parent");
    auto *first = parent->add<Rapture::Node3D>("First");
    auto *second = parent->add<Rapture::Node3D>("Second");
    std::shared_ptr<bool> secondAlive = second->aliveFlag();

    size_t childrenSeen = 0;
    bool secondAliveSeen = false;
    bool moveRefused = false;
    bool addRefused = false;
    int secondFires = 0;

    Rapture::EventConnection firstConnection = first->onDestroy.connect([&](Rapture::Instance *) {
        childrenSeen = parent->children().size();
        secondAliveSeen = *secondAlive;
        scene.destroyInstance(second);
        moveRefused = !second->setParent(scene.root());
        addRefused = parent->add<Rapture::Node3D>("Late") == nullptr;
    });
    Rapture::EventConnection secondConnection = second->onDestroy.connect([&secondFires](Rapture::Instance *) { secondFires++; });

    scene.destroyInstance(parent);

    EXPECT_EQ(childrenSeen, 2u);
    EXPECT_TRUE(secondAliveSeen);
    EXPECT_TRUE(moveRefused);
    EXPECT_TRUE(addRefused);
    EXPECT_EQ(secondFires, 1);
    EXPECT_FALSE(*secondAlive);
}

TEST_F(SceneDestroy, RemovingComponentTellsListenersWhileItIsAttached)
{
    Rapture::Scene scene("SceneDestroy");

    auto *node = scene.root()->add<Rapture::Node3D>("Node");
    auto *component = node->addComponent<TestFlagComponent>();
    std::shared_ptr<bool> alive = component->aliveFlag();

    Rapture::SceneObject *ownerSeen = nullptr;
    Rapture::EventConnection connection =
        component->onDestroy.connect([&ownerSeen, component](Rapture::Instance *) { ownerSeen = component->owner(); });

    node->removeComponent(component);

    EXPECT_EQ(ownerSeen, node);
    EXPECT_FALSE(*alive);
    EXPECT_EQ(node->component<TestFlagComponent>(), nullptr);
}

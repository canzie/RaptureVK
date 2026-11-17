#include <rapture/core/ecs/registry.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <vector>

using namespace Rapture::ecs;

struct Position {
    float x = 0.0f;
    float y = 0.0f;
};

struct Health {
    int value = 100;
};

struct Frozen {};

TEST(EcsEntity, CreatedEntityIsValid)
{
    Registry registry;
    Entity entity = registry.create();

    EXPECT_TRUE(registry.isValid(entity));
    EXPECT_EQ(registry.getAliveCount(), 1u);
}

TEST(EcsEntity, DestroyedEntityIsInvalid)
{
    Registry registry;
    Entity entity = registry.create();
    registry.destroy(entity);

    EXPECT_FALSE(registry.isValid(entity));
    EXPECT_EQ(registry.getAliveCount(), 0u);
}

TEST(EcsEntity, NullAndOutOfRangeEntitiesAreInvalid)
{
    Registry registry;
    registry.create();

    EXPECT_FALSE(registry.isValid(ENTITY_NULL));
    EXPECT_FALSE(registry.isValid(MakeEntity(5, 0)));
}

TEST(EcsEntity, ReusedSlotLeavesStaleHandleInvalid)
{
    Registry registry;
    Entity first = registry.create();
    registry.destroy(first);
    Entity second = registry.create();

    EXPECT_EQ(EntityIndex(first), EntityIndex(second));
    EXPECT_NE(first, second);
    EXPECT_FALSE(registry.isValid(first));
    EXPECT_TRUE(registry.isValid(second));
}

TEST(EcsEntity, GenerationSkipsTheTombstone)
{
    Registry registry;
    Entity entity = registry.create();
    for (uint32_t i = 0; i < ENTITY_GENERATION_MASK + 1; i++) {
        EXPECT_NE(EntityGeneration(entity), ENTITY_GENERATION_TOMBSTONE);
        registry.destroy(entity);
        entity = registry.create();
    }

    EXPECT_EQ(EntityIndex(entity), 0u);
    EXPECT_EQ(EntityGeneration(entity), 1u);
}

TEST(EcsEntity, StaleHandleDoesNotSeeNewOccupantsComponents)
{
    Registry registry;
    Entity first = registry.create();
    registry.destroy(first);
    Entity second = registry.create();
    registry.add<Health>(second);

    EXPECT_FALSE(registry.has<Health>(first));
    EXPECT_TRUE(registry.has<Health>(second));
}

TEST(EcsComponent, AddThenRead)
{
    Registry registry;
    Entity entity = registry.create();
    registry.add<Position>(entity, 1.0f, 2.0f);

    ASSERT_TRUE(registry.has<Position>(entity));
    EXPECT_FLOAT_EQ(registry.read<Position>(entity).x, 1.0f);
    EXPECT_FLOAT_EQ(registry.read<Position>(entity).y, 2.0f);
}

TEST(EcsComponent, TryReadMissingComponentIsNull)
{
    Registry registry;
    Entity entity = registry.create();

    EXPECT_EQ(registry.tryRead<Position>(entity), nullptr);
    registry.add<Health>(entity);
    EXPECT_EQ(registry.tryRead<Position>(entity), nullptr);
}

TEST(EcsComponent, WriteChangesComponent)
{
    Registry registry;
    Entity entity = registry.create();
    registry.add<Health>(entity);

    registry.write<Health>(entity)->value = 3;

    EXPECT_EQ(registry.read<Health>(entity).value, 3);
}

TEST(EcsComponent, SetOverwritesWithoutReattaching)
{
    Registry registry;
    Entity entity = registry.create();
    int constructs = 0;
    int destroys = 0;
    registry.onConstruct<Health>([&constructs](Entity) { constructs++; });
    registry.onDestroy<Health>([&destroys](Entity) { destroys++; });

    registry.set<Health>(entity, 10);
    registry.set<Health>(entity, 20);

    EXPECT_EQ(registry.read<Health>(entity).value, 20);
    EXPECT_EQ(constructs, 1);
    EXPECT_EQ(destroys, 0);
}

TEST(EcsComponent, RemoveDetachesOnlyThatComponent)
{
    Registry registry;
    Entity entity = registry.create();
    registry.add<Position>(entity);
    registry.add<Health>(entity);

    registry.remove<Position>(entity);

    EXPECT_FALSE(registry.has<Position>(entity));
    EXPECT_TRUE(registry.has<Health>(entity));
    EXPECT_FALSE(registry.tryRemove<Position>(entity));
}

TEST(EcsComponent, RemovingKeepsOtherEntitiesComponents)
{
    Registry registry;
    std::vector<Entity> entities;
    for (int i = 0; i < 8; i++) {
        Entity entity = registry.create();
        registry.add<Health>(entity, i);
        entities.push_back(entity);
    }

    registry.remove<Health>(entities[2]);
    registry.remove<Health>(entities[5]);

    for (int i = 0; i < 8; i++) {
        if (i == 2 || i == 5) {
            EXPECT_FALSE(registry.has<Health>(entities[i]));
        } else {
            EXPECT_EQ(registry.read<Health>(entities[i]).value, i);
        }
    }
}

TEST(EcsComponent, EmptyComponentIsMembershipOnly)
{
    Registry registry;
    Entity entity = registry.create();
    registry.add<Frozen>(entity);

    EXPECT_TRUE(registry.has<Frozen>(entity));
    registry.remove<Frozen>(entity);
    EXPECT_FALSE(registry.has<Frozen>(entity));
}

TEST(EcsComponent, MaskTracksAttachedComponents)
{
    Registry registry;
    Entity entity = registry.create();
    registry.add<Position>(entity);
    registry.add<Frozen>(entity);

    EXPECT_EQ(registry.getComponentMask(entity), (ComponentBits<Position, Frozen>()));
    EXPECT_TRUE((registry.hasAll<Position, Frozen>(entity)));
    EXPECT_FALSE((registry.hasAll<Position, Health>(entity)));
    EXPECT_TRUE((registry.hasAny<Health, Frozen>(entity)));
}

TEST(EcsSignal, ConstructFiresWithComponentReadable)
{
    Registry registry;
    int seen = -1;
    registry.onConstruct<Health>([&registry, &seen](Entity entity) { seen = registry.read<Health>(entity).value; });

    Entity entity = registry.create();
    registry.add<Health>(entity, 42);

    EXPECT_EQ(seen, 42);
}

TEST(EcsSignal, DestroyFiresWithComponentStillReadable)
{
    Registry registry;
    int seen = -1;
    registry.onDestroy<Health>([&registry, &seen](Entity entity) { seen = registry.read<Health>(entity).value; });

    Entity entity = registry.create();
    registry.add<Health>(entity, 7);
    registry.destroy(entity);

    EXPECT_EQ(seen, 7);
}

TEST(EcsSignal, DestroyingEntityFiresEveryComponentsSignal)
{
    Registry registry;
    int destroyed = 0;
    registry.onDestroy<Position>([&destroyed](Entity) { destroyed++; });
    registry.onDestroy<Health>([&destroyed](Entity) { destroyed++; });
    registry.onDestroy<Frozen>([&destroyed](Entity) { destroyed++; });

    Entity entity = registry.create();
    registry.add<Position>(entity);
    registry.add<Health>(entity);
    registry.add<Frozen>(entity);
    registry.destroy(entity);

    EXPECT_EQ(destroyed, 3);
}

TEST(EcsSignal, ScopedConnectionStopsOnDestruction)
{
    Registry registry;
    int constructs = 0;
    {
        SignalConnection connection = registry.onConstructScoped<Health>([&constructs](Entity) { constructs++; });
        registry.add<Health>(registry.create());
    }
    registry.add<Health>(registry.create());

    EXPECT_EQ(constructs, 1);
}

TEST(EcsView, YieldsOnlyEntitiesWithEveryComponent)
{
    Registry registry;
    Entity both = registry.create();
    registry.add<Position>(both);
    registry.add<Health>(both);
    Entity onlyPosition = registry.create();
    registry.add<Position>(onlyPosition);
    Entity onlyHealth = registry.create();
    registry.add<Health>(onlyHealth);

    std::vector<Entity> seen;
    for (auto [entity, position, health] : registry.read<Position, Health>()) {
        seen.push_back(entity);
    }

    EXPECT_EQ(seen, std::vector<Entity>{both});
}

TEST(EcsView, EmptyWhenAComponentWasNeverUsed)
{
    Registry registry;
    registry.add<Position>(registry.create());

    int count = 0;
    for (auto [entity, position, health] : registry.read<Position, Health>()) {
        (void)entity;
        count++;
    }

    EXPECT_EQ(count, 0);
}

TEST(EcsView, WithAndWithoutFilter)
{
    Registry registry;
    Entity frozen = registry.create();
    registry.add<Health>(frozen);
    registry.add<Frozen>(frozen);
    Entity thawed = registry.create();
    registry.add<Health>(thawed);

    std::vector<Entity> withFrozen;
    for (auto [entity, health] : registry.read<Health>().with<Frozen>()) {
        withFrozen.push_back(entity);
    }
    std::vector<Entity> withoutFrozen;
    for (auto [entity, health] : registry.read<Health>().without<Frozen>()) {
        withoutFrozen.push_back(entity);
    }

    EXPECT_EQ(withFrozen, std::vector<Entity>{frozen});
    EXPECT_EQ(withoutFrozen, std::vector<Entity>{thawed});
}

TEST(EcsView, MutableViewWritesThrough)
{
    Registry registry;
    for (int i = 0; i < 4; i++) {
        registry.add<Health>(registry.create(), i);
    }

    for (auto [entity, health] : registry.mutableView<Health>()) {
        health.value *= 10;
    }

    int sum = 0;
    for (auto [entity, health] : registry.read<Health>()) {
        sum += health.value;
    }
    EXPECT_EQ(sum, 60);
}

TEST(EcsTypeId, DistinctTypesGetDistinctIds)
{
    EXPECT_NE(ComponentType<Position>(), ComponentType<Health>());
    EXPECT_NE(ComponentType<Position>(), ComponentType<Frozen>());
    EXPECT_NE(ComponentType<Health>(), ComponentType<Frozen>());
}

TEST(EcsTypeId, SameTypeKeepsItsId)
{
    EXPECT_EQ(ComponentType<Position>(), ComponentType<Position>());
}

TEST(EcsTypeId, SignatureLookupMatchesTemplateId)
{
    ComponentTypeId id = ComponentType<Health>();

    EXPECT_EQ(ComponentTypeIdForSignature(ComponentTypeSignature<Health>()), id);
}

TEST(EcsTypeId, IdsAreSharedBetweenRegistries)
{
    Registry first;
    Registry second;
    Entity a = first.create();
    Entity b = second.create();
    first.add<Health>(a);
    second.add<Health>(b);

    EXPECT_EQ(first.getComponentMask(a), second.getComponentMask(b));
}

TEST(EcsChurn, RandomCreateDestroyKeepsHandlesHonest)
{
    Registry registry;
    std::mt19937 random(1234);
    std::vector<Entity> alive;
    std::vector<Entity> dead;

    for (int step = 0; step < 20000; step++) {
        bool destroy = !alive.empty() && (random() % 3 == 0);
        if (destroy) {
            size_t pick = random() % alive.size();
            Entity entity = alive[pick];
            alive[pick] = alive.back();
            alive.pop_back();
            registry.destroy(entity);
            dead.push_back(entity);
        } else {
            Entity entity = registry.create();
            registry.add<Health>(entity, static_cast<int>(entity));
            alive.push_back(entity);
        }
    }

    EXPECT_EQ(registry.getAliveCount(), alive.size());
    for (Entity entity : alive) {
        ASSERT_TRUE(registry.isValid(entity));
        EXPECT_EQ(registry.read<Health>(entity).value, static_cast<int>(entity));
    }

    // every slot here was reused far fewer than 4096 times, so no dead handle can alias a live one
    for (Entity entity : dead) {
        if (std::find(alive.begin(), alive.end(), entity) == alive.end()) {
            EXPECT_FALSE(registry.isValid(entity));
        }
    }

    uint32_t viewed = 0;
    for (auto [entity, health] : registry.read<Health>()) {
        (void)entity;
        viewed++;
    }
    EXPECT_EQ(viewed, alive.size());
}

TEST(EcsChurn, ClearInvalidatesEverything)
{
    Registry registry;
    std::vector<Entity> entities;
    for (int i = 0; i < 100; i++) {
        Entity entity = registry.create();
        registry.add<Position>(entity);
        entities.push_back(entity);
    }

    registry.clear();

    EXPECT_EQ(registry.getAliveCount(), 0u);
    for (Entity entity : entities) {
        EXPECT_FALSE(registry.isValid(entity));
    }
    int count = 0;
    for (auto [entity, position] : registry.read<Position>()) {
        (void)entity;
        count++;
    }
    EXPECT_EQ(count, 0);
}

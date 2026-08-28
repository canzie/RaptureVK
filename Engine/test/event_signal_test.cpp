#include "core/events/EventSignal.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using Rapture::EventConnection;
using Rapture::EventSignal;

TEST(EventSignal, FiresConnectedCallback)
{
    EventSignal<void()> signal;
    int calls = 0;

    EventConnection connection = signal.connect([&calls]() { ++calls; });
    signal.fire();

    EXPECT_EQ(calls, 1);
}

TEST(EventSignal, FiresInConnectionOrder)
{
    EventSignal<void()> signal;
    std::vector<int> order;

    EventConnection first = signal.connect([&order]() { order.push_back(1); });
    EventConnection second = signal.connect([&order]() { order.push_back(2); });
    EventConnection third = signal.connect([&order]() { order.push_back(3); });
    signal.fire();

    EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
}

TEST(EventSignal, ForwardsArguments)
{
    EventSignal<void(int, std::string)> signal;
    int received = 0;
    std::string text;

    EventConnection connection = signal.connect([&](int value, std::string message) {
        received = value;
        text = std::move(message);
    });
    signal.fire(7, "hello");

    EXPECT_EQ(received, 7);
    EXPECT_EQ(text, "hello");
}

TEST(EventSignal, DisconnectStopsCallback)
{
    EventSignal<void()> signal;
    int calls = 0;

    EventConnection connection = signal.connect([&calls]() { ++calls; });
    connection.disconnect();
    signal.fire();

    EXPECT_EQ(calls, 0);
}

TEST(EventSignal, DisconnectIsIdempotent)
{
    EventSignal<void()> signal;
    int calls = 0;

    EventConnection connection = signal.connect([&calls]() { ++calls; });
    connection.disconnect();
    connection.disconnect();
    signal.fire();

    EXPECT_EQ(calls, 0);
}

TEST(EventSignal, ConnectionDisconnectsOnDestruction)
{
    EventSignal<void()> signal;
    int calls = 0;

    {
        EventConnection connection = signal.connect([&calls]() { ++calls; });
        signal.fire();
    }
    signal.fire();

    EXPECT_EQ(calls, 1);
}

TEST(EventSignal, MovedConnectionKeepsTheSubscription)
{
    EventSignal<void()> signal;
    int calls = 0;

    EventConnection moved;
    {
        EventConnection connection = signal.connect([&calls]() { ++calls; });
        moved = std::move(connection);
    }
    signal.fire();

    EXPECT_TRUE(moved.connected());
    EXPECT_EQ(calls, 1);
}

TEST(EventSignal, ConnectedReportsSubscription)
{
    EventSignal<void()> signal;

    EventConnection connection = signal.connect([]() {});
    EXPECT_TRUE(connection.connected());

    connection.disconnect();
    EXPECT_FALSE(connection.connected());
}

TEST(EventSignal, DefaultConnectionIsNotConnected)
{
    EventConnection connection;

    EXPECT_FALSE(connection.connected());
}

TEST(EventSignal, OnceFiresOnlyOnce)
{
    EventSignal<void()> signal;
    int calls = 0;

    EventConnection connection = signal.once([&calls]() { ++calls; });
    signal.fire();
    signal.fire();

    EXPECT_EQ(calls, 1);
    EXPECT_FALSE(connection.connected());
}

TEST(EventSignal, DetachedOnceFiresOnlyOnce)
{
    EventSignal<void()> signal;
    int calls = 0;

    signal.detachedOnce([&calls]() { ++calls; });
    signal.fire();
    signal.fire();

    EXPECT_EQ(calls, 1);
}

TEST(EventSignal, OnceConnectedDuringFireSurvivesToTheNextFire)
{
    EventSignal<void()> signal;
    int calls = 0;
    EventConnection late;

    EventConnection connection = signal.connect([&]() {
        if (!late.connected()) {
            late = signal.once([&calls]() { ++calls; });
        }
    });

    signal.fire();
    EXPECT_EQ(calls, 0);

    signal.fire();
    EXPECT_EQ(calls, 1);
}

TEST(EventSignal, DisconnectDuringFireSkipsAPendingCallback)
{
    EventSignal<void()> signal;
    int second = 0;
    EventConnection secondConnection;

    EventConnection firstConnection = signal.connect([&]() { secondConnection.disconnect(); });
    secondConnection = signal.connect([&second]() { ++second; });

    signal.fire();

    EXPECT_EQ(second, 0);
}

TEST(EventSignal, CallbackMayDisconnectItselfDuringFire)
{
    EventSignal<void()> signal;
    int calls = 0;
    EventConnection connection;

    connection = signal.connect([&]() {
        ++calls;
        connection.disconnect();
    });

    signal.fire();
    signal.fire();

    EXPECT_EQ(calls, 1);
}

TEST(EventSignal, ConnectDuringFireRunsOnTheNextFire)
{
    EventSignal<void()> signal;
    int late = 0;
    EventConnection lateConnection;

    EventConnection connection = signal.connect([&]() {
        if (!lateConnection.connected()) {
            lateConnection = signal.connect([&late]() { ++late; });
        }
    });

    signal.fire();
    EXPECT_EQ(late, 0);

    signal.fire();
    EXPECT_EQ(late, 1);
}

TEST(EventSignal, FiresMayNest)
{
    EventSignal<void()> signal;
    int calls = 0;

    EventConnection connection = signal.connect([&]() {
        ++calls;
        if (calls == 1) {
            signal.fire();
        }
    });

    signal.fire();

    EXPECT_EQ(calls, 2);
}

TEST(EventSignal, StaleConnectionDoesNotReleaseARecycledSlot)
{
    EventSignal<void()> signal;
    int recycled = 0;

    EventConnection consumed = signal.once([]() {});
    signal.fire();
    ASSERT_FALSE(consumed.connected());

    EventConnection reused = signal.connect([&recycled]() { ++recycled; });
    consumed.disconnect();
    signal.fire();

    EXPECT_TRUE(reused.connected());
    EXPECT_EQ(recycled, 1);
}

TEST(EventSignal, ConnectionOutlivesItsSignal)
{
    auto signal = std::make_unique<EventSignal<void()>>();
    EventConnection connection = signal->connect([]() {});

    signal.reset();

    EXPECT_FALSE(connection.connected());
    EXPECT_NO_FATAL_FAILURE(connection.disconnect());
}

TEST(EventSignal, CountsLiveConnections)
{
    EventSignal<void()> signal;

    EXPECT_EQ(signal.connectionCount(), 0u);

    EventConnection first = signal.connect([]() {});
    EventConnection second = signal.connect([]() {});
    EXPECT_EQ(signal.connectionCount(), 2u);

    first.disconnect();
    EXPECT_EQ(signal.connectionCount(), 1u);

    {
        EventConnection third = signal.connect([]() {});
        EXPECT_EQ(signal.connectionCount(), 2u);
    }
    EXPECT_EQ(signal.connectionCount(), 1u);
}

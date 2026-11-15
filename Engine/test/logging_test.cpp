#include <gtest/gtest.h>

#include <rapture/core/utils/Log.h>

namespace {

// These tests link the engine, and engine code logs. Every logger is a null shared_ptr until
// Log::Init, so without this the first test to take a path that logs dereferences one.
class LoggingEnvironment : public ::testing::Environment {
  public:
    void SetUp() override { Rapture::Log::Init(); }
    void TearDown() override { Rapture::Log::Shutdown(); }
};

const ::testing::Environment *gLoggingEnvironment = ::testing::AddGlobalTestEnvironment(new LoggingEnvironment);

TEST(LoggingTest, EveryLoggerIsUpBeforeAnyTestRuns)
{
    EXPECT_NE(Rapture::Log::GetCoreLogger(), nullptr);
    EXPECT_NE(Rapture::Log::GetClientLogger(), nullptr);
    EXPECT_NE(Rapture::Log::GetRenderLogger(), nullptr);
    EXPECT_NE(Rapture::Log::GetPhysicsLogger(), nullptr);
    EXPECT_NE(Rapture::Log::GetAudioLogger(), nullptr);
}

TEST(LoggingTest, LoggingAnErrorDoesNotCrash)
{
    RP_CORE_ERROR("logging is up, this line is expected in the test output");
}

} // namespace

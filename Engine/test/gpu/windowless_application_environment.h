#ifndef RAPTURE__WINDOWLESS_APPLICATION_ENVIRONMENT_H
#define RAPTURE__WINDOWLESS_APPLICATION_ENVIRONMENT_H

#include <rapture/app/Application.h>
#include <rapture/core/utils/Log.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>

/**
 * @brief One windowless application shared by every test in the executable
 */
class WindowlessApplicationEnvironment : public ::testing::Environment {
  public:
    void SetUp() override
    {
        Rapture::Log::Init();
        enableSynchronizationValidation();
        m_application = std::make_unique<Rapture::Application>(Rapture::ApplicationConfig{});
    }

    void TearDown() override
    {
        m_application.reset();
        Rapture::Log::Shutdown();
    }

  private:
    /**
     * @brief Turns on the validation layer's synchronization checks, read when the instance is created
     */
    static void enableSynchronizationValidation()
    {
#ifdef _WIN32
        _putenv_s("VK_KHRONOS_VALIDATION_VALIDATE_SYNC", "true");
#else
        setenv("VK_KHRONOS_VALIDATION_VALIDATE_SYNC", "true", 1);
#endif // _WIN32
    }

  private:
    std::unique_ptr<Rapture::Application> m_application;
};

/**
 * @brief A test on the windowless application that fails if the validation layer reports an error during it
 */
class WindowlessGpuTest : public ::testing::Test {
  protected:
    void SetUp() override { m_validationErrorsBefore = vulkanContext().getValidationErrorCount(); }

    void TearDown() override
    {
        EXPECT_EQ(vulkanContext().getValidationErrorCount(), m_validationErrorsBefore)
            << "the validation layer reported errors, see the log";
    }

    static Rapture::VulkanContext &vulkanContext() { return Rapture::Application::getInstance().getVulkanContext(); }

  private:
    uint32_t m_validationErrorsBefore = 0;
};

inline const ::testing::Environment *gWindowlessApplicationEnvironment =
    ::testing::AddGlobalTestEnvironment(new WindowlessApplicationEnvironment);

#endif // RAPTURE__WINDOWLESS_APPLICATION_ENVIRONMENT_H

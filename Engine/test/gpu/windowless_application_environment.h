#ifndef RAPTURE__WINDOWLESS_APPLICATION_ENVIRONMENT_H
#define RAPTURE__WINDOWLESS_APPLICATION_ENVIRONMENT_H

#include <rapture/app/Application.h>
#include <rapture/core/utils/Log.h>

#include <gtest/gtest.h>

#include <memory>

/**
 * @brief One windowless application shared by every test in the executable
 */
class WindowlessApplicationEnvironment : public ::testing::Environment {
  public:
    void SetUp() override
    {
        Rapture::Log::Init();
        m_application = std::make_unique<Rapture::Application>(Rapture::ApplicationConfig{});
    }

    void TearDown() override
    {
        m_application.reset();
        Rapture::Log::Shutdown();
    }

  private:
    std::unique_ptr<Rapture::Application> m_application;
};

inline const ::testing::Environment *gWindowlessApplicationEnvironment =
    ::testing::AddGlobalTestEnvironment(new WindowlessApplicationEnvironment);

#endif // RAPTURE__WINDOWLESS_APPLICATION_ENVIRONMENT_H

#ifndef RAPTURE__ASSET_MANAGER_FIXTURE_H
#define RAPTURE__ASSET_MANAGER_FIXTURE_H

#include "assets/asset_manager/AssetManager.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

/**
 * @brief A test with the asset manager up and a scratch folder of its own, without a window or a device
 */
class AssetManagerFixture : public ::testing::Test {
  protected:
    static constexpr uint32_t FRAMES_IN_FLIGHT = 2;

    void SetUp() override
    {
        const ::testing::TestInfo *test = ::testing::UnitTest::GetInstance()->current_test_info();
        m_scratchFolder =
            std::filesystem::temp_directory_path() / "rapture_tests" / (std::string(test->test_suite_name()) + "_" + test->name());
        std::filesystem::remove_all(m_scratchFolder);
        std::filesystem::create_directories(m_scratchFolder);

        Rapture::AssetManager::init(nullptr, FRAMES_IN_FLIGHT);
    }

    void TearDown() override
    {
        Rapture::AssetManager::shutdown();
        std::filesystem::remove_all(m_scratchFolder);
    }

    /**
     * @brief Writes a file into the scratch folder
     * @param fileName The file name inside the scratch folder
     * @param contents The file contents
     * @return The path of the written file
     */
    std::filesystem::path writeScratchFile(std::string_view fileName, std::string_view contents) const
    {
        std::filesystem::path path = m_scratchFolder / fileName;
        std::ofstream file(path, std::ios::binary);
        file << contents;
        return path;
    }

    /**
     * @brief Runs the asset manager's per-frame update
     * @param frameCount The number of frames to run
     */
    static void runAssetManagerFrames(uint32_t frameCount)
    {
        for (uint32_t frame = 0; frame < frameCount; frame++) {
            Rapture::AssetManager::onUpdate();
        }
    }

    std::filesystem::path m_scratchFolder;
};

#endif // RAPTURE__ASSET_MANAGER_FIXTURE_H

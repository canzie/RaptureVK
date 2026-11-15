#include "asset_manager_fixture.h"

using Rapture::AssetHandle;
using Rapture::AssetImportFileRequest;
using Rapture::AssetManager;
using Rapture::AssetRef;

using AssetUnloadTest = AssetManagerFixture;

static constexpr std::string_view SCRIPT_SOURCE = "return 1";

static AssetRef s_importScratchScript(const std::filesystem::path &source, const std::filesystem::path &outputFolder)
{
    return AssetManager::importAsset(AssetImportFileRequest{.source = source, .output = outputFolder});
}

TEST_F(AssetUnloadTest, ReleasedOnceIsEvicted)
{
    AssetRef script = s_importScratchScript(writeScratchFile("script.lua", SCRIPT_SOURCE), m_scratchFolder);
    ASSERT_TRUE(script);
    AssetHandle handle = script->handle();

    runAssetManagerFrames(1);
    ASSERT_TRUE(AssetManager::isAssetLoaded(handle));

    script = AssetRef();
    runAssetManagerFrames(1);

    EXPECT_FALSE(AssetManager::isAssetLoaded(handle));
    EXPECT_TRUE(static_cast<bool>(AssetManager::getAssetMetadata(handle)));
}

TEST_F(AssetUnloadTest, ReleasedTwiceInOneFrameIsEvictedOnce)
{
    AssetRef script = s_importScratchScript(writeScratchFile("script.lua", SCRIPT_SOURCE), m_scratchFolder);
    ASSERT_TRUE(script);
    AssetHandle handle = script->handle();

    runAssetManagerFrames(1);

    script = AssetRef();
    script = AssetManager::getAsset(handle);
    ASSERT_TRUE(script);
    script = AssetRef();

    runAssetManagerFrames(1);

    EXPECT_FALSE(AssetManager::isAssetLoaded(handle));
    EXPECT_TRUE(static_cast<bool>(AssetManager::getAssetMetadata(handle)));
}

TEST_F(AssetUnloadTest, EvictedAssetLoadsAgain)
{
    AssetRef script = s_importScratchScript(writeScratchFile("script.lua", SCRIPT_SOURCE), m_scratchFolder);
    ASSERT_TRUE(script);
    AssetHandle handle = script->handle();

    runAssetManagerFrames(1);
    script = AssetRef();
    runAssetManagerFrames(1);
    ASSERT_FALSE(AssetManager::isAssetLoaded(handle));

    script = AssetManager::getAsset(handle);

    ASSERT_TRUE(script);
    EXPECT_EQ(script->handle(), handle);
    EXPECT_TRUE(AssetManager::isAssetLoaded(handle));
}

TEST_F(AssetUnloadTest, HeldAssetIsNotEvicted)
{
    AssetRef script = s_importScratchScript(writeScratchFile("script.lua", SCRIPT_SOURCE), m_scratchFolder);
    ASSERT_TRUE(script);
    AssetHandle handle = script->handle();

    AssetRef second = script;
    script = AssetRef();
    runAssetManagerFrames(2);

    EXPECT_TRUE(AssetManager::isAssetLoaded(handle));
}

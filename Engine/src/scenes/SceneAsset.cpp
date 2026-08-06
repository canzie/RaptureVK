#include "SceneAsset.h"

#include "logging/Log.h"
#include "scenes/Scene.h"

namespace Rapture {

SceneAsset::SceneAsset(const Scene &scene)
{
    SerialDocument builder;
    scene.serialize(builder.root());

    // parsed back so the member is always readable, whatever produced it
    m_document = SerialDocument::parse(builder.toText());
}

std::vector<uint8_t> SceneAsset::serialize() const
{
    std::string text = m_document.toText();
    if (text.empty()) {
        RP_CORE_ERROR("scene document could not be written");
        return {};
    }

    return std::vector<uint8_t>(text.begin(), text.end());
}

std::unique_ptr<SceneAsset> SceneAsset::deserialize(std::span<const uint8_t> blob)
{
    std::string_view text(reinterpret_cast<const char *>(blob.data()), blob.size());

    SerialDocument document = SerialDocument::parse(text);
    if (!document.rootView().valid()) {
        RP_CORE_ERROR("scene blob does not hold a readable document");
        return nullptr;
    }

    std::unique_ptr<SceneAsset> asset(new SceneAsset());
    asset->m_document = std::move(document);
    return asset;
}

} // namespace Rapture

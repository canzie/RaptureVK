#ifndef RAPTURE__SCRIPT_COMPONENT_H
#define RAPTURE__SCRIPT_COMPONENT_H

#include <rapture/assets/asset_manager/AssetCommon.h>
#include <rapture/assets/scripts/AScript.h>
#include <rapture/core/utils/Ref.h>
#include <rapture/scene/instances/SceneComponent.h>

#include <string_view>

namespace Rapture {

/**
 * @brief The Lua the object this is attached to runs.
 *
 * The Lua itself is a script asset, so objects running the same script share one copy of it. What
 * differs between them belongs here rather than in the asset.
 */
class ScriptComponent : public SceneComponent {
  public:
    ScriptComponent(Scene &scene, std::string_view name);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    void serialize(WriteNode node) const override;
    void deserialize(ReadNode node) override;

    AssetHandle script() const { return m_script; }

    /**
     * @brief Replaces the script this runs
     * @param script The script asset to run
     */
    void setScript(AssetHandle script);

    /**
     * @brief The Lua this runs
     * @return The Lua, empty while this has no script
     */
    std::string_view source() const;

  protected:
    void onAttach() override;
    void onDetach() override;
    void onReady() override;

  private:
    AssetHandle m_script = INVALID_ASSET_HANDLE;
    Ref<AScript> m_scriptAsset;
};

} // namespace Rapture

#endif // RAPTURE__SCRIPT_COMPONENT_H

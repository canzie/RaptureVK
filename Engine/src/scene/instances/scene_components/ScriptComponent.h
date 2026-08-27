#ifndef RAPTURE__SCRIPT_COMPONENT_H
#define RAPTURE__SCRIPT_COMPONENT_H

#include "scene/instances/SceneComponent.h"

#include <filesystem>
#include <string>

namespace Rapture {

/**
 * @brief The Lua the object this is attached to runs.
 */
class ScriptComponent : public SceneComponent {
  public:
    ScriptComponent(Scene &scene, std::string_view name);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    void serialize(WriteNode node) const override;
    void deserialize(ReadNode node) override;

    std::string_view source() const { return m_source; }

    /**
     * @brief Replaces the script this runs
     * @param source The Lua to run
     */
    void setSource(std::string_view source);

    /**
     * @brief The file this script is checked out to for editing outside the editor
     * @return The path, empty when the script only lives in the document
     */
    const std::filesystem::path &externalPath() const { return m_externalPath; }

    /**
     * @brief Names the file this script is checked out to
     * @param path The path, empty to keep the script in the document alone
     */
    void setExternalPath(std::filesystem::path path);

    /**
     * @brief Whether the checked out file has changed since this script was last read from it
     */
    bool isExternalDirty() const { return m_externalDirty; }

    void setExternalDirty(bool dirty) { m_externalDirty = dirty; }

  protected:
    void onAttach() override;
    void onDetach() override;
    void onReady() override;

  private:
    std::string m_source;
    std::filesystem::path m_externalPath;
    bool m_externalDirty = false;
};

} // namespace Rapture

#endif // RAPTURE__SCRIPT_COMPONENT_H

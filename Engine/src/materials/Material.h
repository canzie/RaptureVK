#ifndef RAPTURE__MATERIAL_H
#define RAPTURE__MATERIAL_H

#include "MaterialData.h"
#include "MaterialParameters.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <initializer_list>

namespace Rapture {

class BaseMaterial : public std::enable_shared_from_this<BaseMaterial> {
  public:
    BaseMaterial(const std::string &name, std::initializer_list<ParameterID> editableParams, const MaterialData &defaults);
    ~BaseMaterial() = default;

    const std::string &getName() const { return m_name; }
    const MaterialData &getDefaults() const { return m_defaults; }
    bool canEdit(ParameterID id) const { return m_editableParams.find(id) != m_editableParams.end(); }
    const std::unordered_set<ParameterID> &getEditableParams() const { return m_editableParams; }

  private:
    std::string m_name;
    std::unordered_set<ParameterID> m_editableParams;
    MaterialData m_defaults;

    friend class MaterialInstance;
    friend class MaterialManager;
};

class MaterialManager {
  public:
    static void init();
    static void shutdown();

    static std::shared_ptr<BaseMaterial> getMaterial(const std::string &name);
    static std::shared_ptr<BaseMaterial> createMaterial(const std::string &name,
                                                         std::initializer_list<ParameterID> editableParams,
                                                         const MaterialData &defaults);
    static uint32_t getDefaultTextureIndex();
    static void printMaterialNames();

  private:
    static void createDefaultMaterials();

    static bool s_initialized;
    static uint32_t s_defaultTextureIndex;
    static std::unordered_map<std::string, std::shared_ptr<BaseMaterial>> s_materials;
};

} // namespace Rapture

#endif // RAPTURE__MATERIAL_H

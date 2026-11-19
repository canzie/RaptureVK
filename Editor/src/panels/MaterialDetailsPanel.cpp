#include "MaterialDetailsPanel.h"

#include "Icons.h"
#include "panels/NodeEditorPanel.h"
#include "ui/GraphVisuals.h"

#include <rapture/assets/asset_manager/AssetManager.h>
#include <rapture/assets/materials/AMaterialInstance.h>
#include <rapture/assets/materials/Material.h>
#include <rapture/assets/materials/MaterialInstance.h>
#include <rapture/assets/textures/ATexture.h>

#include <components/ui_scope.h>

#include <array>
#include <cctype>
#include <span>
#include <string>
#include <vector>

static constexpr float CONTENT_TOP = 4.0f;

static std::string s_parameterLabel(std::string_view id)
{
    std::string label(id);
    bool wordStart = true;
    for (char &c : label) {
        if (c == '_') {
            c = ' ';
            wordStart = true;
            continue;
        }
        if (wordStart) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        wordStart = false;
    }
    return label;
}

static void s_readParameter(const Rapture::MaterialInstance &material, const Rapture::MaterialParameterInfo &parameter,
                            std::array<float, 4> &values)
{
    switch (parameter.type) {
    case Rapture::PinType::FLOAT:
        values[0] = material.getParameter<float>(parameter.id);
        break;
    case Rapture::PinType::INT:
        values[0] = static_cast<float>(material.getParameter<int32_t>(parameter.id));
        break;
    case Rapture::PinType::VEC2: {
        const glm::vec2 v = material.getParameter<glm::vec2>(parameter.id);
        values = {v.x, v.y, 0.0f, 0.0f};
        break;
    }
    case Rapture::PinType::VEC3: {
        const glm::vec3 v = material.getParameter<glm::vec3>(parameter.id);
        values = {v.x, v.y, v.z, 0.0f};
        break;
    }
    case Rapture::PinType::VEC4: {
        const glm::vec4 v = material.getParameter<glm::vec4>(parameter.id);
        values = {v.x, v.y, v.z, v.w};
        break;
    }
    case Rapture::PinType::TEXTURE:
        break;
    }
}

static void s_writeParameter(Rapture::MaterialInstance &material, const Rapture::MaterialParameterInfo &parameter,
                             const std::array<float, 4> &values)
{
    switch (parameter.type) {
    case Rapture::PinType::FLOAT:
        material.setParameter(parameter.id, values[0]);
        break;
    case Rapture::PinType::INT:
        material.setParameter(parameter.id, static_cast<int32_t>(values[0]));
        break;
    case Rapture::PinType::VEC2:
        material.setParameter(parameter.id, glm::vec2(values[0], values[1]));
        break;
    case Rapture::PinType::VEC3:
        material.setParameter(parameter.id, glm::vec3(values[0], values[1], values[2]));
        break;
    case Rapture::PinType::VEC4:
        material.setParameter(parameter.id, glm::vec4(values[0], values[1], values[2], values[3]));
        break;
    case Rapture::PinType::TEXTURE:
        break;
    }
}

/**
 * @brief The section showing the open material: its base and the values of the parameters it exposes
 */
class MaterialSection : public PropertySection {
  public:
    explicit MaterialSection(Rapture::MaterialInstance &material) : m_material(material)
    {
        if (Rapture::BaseMaterial *base = material.getBaseMaterial()) {
            m_parameters = base->getParameters();
        }
        m_values.resize(m_parameters.size());
        m_texturePickers.resize(m_parameters.size());
    }

    const char *title() const override { return m_material.getName().c_str(); }
    const char *icon() const override { return Icons::SVG_MATERIAL; }

    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override
    {
        fieldTable(ch, [this](Amethyst::TableScope &t) {
            const Rapture::BaseMaterial *base = m_material.getBaseMaterial();
            rowText(t, "Base material", base != nullptr ? std::string_view(base->getName()) : std::string_view("None"));

            for (size_t i = 0; i < m_parameters.size(); ++i) {
                const Rapture::MaterialParameterInfo &parameter = m_parameters[i];
                const std::string label = s_parameterLabel(parameter.id);

                if (parameter.type == Rapture::PinType::TEXTURE) {
                    rowAssetPicker(t, label, m_texturePickers[i], AssetPickerConfig{.types = {Rapture::ASSET_TEXTURE}},
                                   [this, i](Rapture::AssetHandle handle) {
                                       Rapture::Ref<Rapture::ATexture> texture = Rapture::AssetManager::getAsset<Rapture::ATexture>(handle);
                                       if (texture) {
                                           m_material.setParameter(m_parameters[i].id, std::move(texture));
                                       }
                                   });
                    const Rapture::Ref<Rapture::ATexture> bound = m_material.getTextureRef(parameter.id);
                    m_texturePickers[i]->setAsset(bound ? bound.get()->handle() : Rapture::INVALID_ASSET_HANDLE);
                    continue;
                }

                s_readParameter(m_material, parameter, m_values[i]);
                const std::span<float> values(m_values[i].data(), Rapture::graph_pinTypeComponents(parameter.type));
                rowFloatFields(t, label, values, parameter.type == Rapture::PinType::INT,
                               [this, i]() { s_writeParameter(m_material, m_parameters[i], m_values[i]); });
            }
        });
    }

    void sync() override {}

  private:
    Rapture::MaterialInstance &m_material;
    std::vector<Rapture::MaterialParameterInfo> m_parameters;
    std::vector<std::array<float, 4>> m_values;
    std::vector<std::optional<AssetPicker>> m_texturePickers;
};

/**
 * @brief The section showing one graph node: its variant, its texture and its values
 */
class GraphNodeSection : public PropertySection {
  public:
    GraphNodeSection(NodeEditorPanel &graph, uint32_t nodeId)
        : m_graph(graph), m_nodeId(nodeId), m_title(graph.getNodeTitle(nodeId)), m_icon(GraphCategory_icon(graph.getNodeCategory(nodeId)))
    {
    }

    const char *title() const override { return m_title.c_str(); }
    const char *icon() const override { return m_icon; }

    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override
    {
        fieldTable(ch, [this](Amethyst::TableScope &t) {
            bool anyRow = false;

            const std::vector<std::string> variants = m_graph.getNodeVariantLabels(m_nodeId);
            if (!variants.empty()) {
                rowDropdown(t, "Type", m_graph.getNodeVariantLabel(m_nodeId), variants,
                            [this](int index) { m_graph.setNodeVariant(m_nodeId, static_cast<size_t>(index)); });
                anyRow = true;
            }

            if (m_graph.samplesTextureAsset(m_nodeId)) {
                rowAssetPicker(t, "Texture", m_texturePicker, AssetPickerConfig{.types = {Rapture::ASSET_TEXTURE}},
                               [this](Rapture::AssetHandle handle) { m_graph.setNodeTexture(m_nodeId, handle); });
                m_texturePicker->setAsset(m_graph.getNodeTexture(m_nodeId));
                anyRow = true;
            }

            for (const NodeEditorPanel::NodeValueRow &row : m_graph.getNodeValueRows(m_nodeId)) {
                if (row.connected) {
                    rowText(t, row.label, "Connected");
                } else {
                    rowFloatFields(t, row.label, row.values, row.wholeNumbers, [this]() { m_graph.refreshNodeFields(m_nodeId); });
                }
                anyRow = true;
            }

            if (!anyRow) {
                rowText(t, "Category", GraphCategory_name(m_graph.getNodeCategory(m_nodeId)));
            }
        });
    }

    void sync() override {}

  private:
    NodeEditorPanel &m_graph;
    uint32_t m_nodeId;
    std::string m_title;
    const char *m_icon;
    std::optional<AssetPicker> m_texturePicker;
};

MaterialDetailsPanel::MaterialDetailsPanel(const WorkspaceContext &context, Rapture::AssetHandle material, NodeEditorPanel &graph)
    : Panel("Details", context), m_material(Rapture::AssetManager::getAsset(material)), m_graph(graph)
{
    m_icon = Icons::SVG_PROPERTIES;
}

void MaterialDetailsPanel::build(Amethyst::UIScope &scope)
{
    (void)scope;

    m_sections.emplace(*m_root, Amethyst::ScrollingFrameProperties{
                                    .classes = {"panel"},
                                    .base =
                                        {
                                            .clipsDescendants = true,
                                            .position = Amethyst::UDim2::fromOffset(0.0f, CONTENT_TOP),
                                            .size = Amethyst::UDim2(1.0f, 0.0f, 1.0f, -CONTENT_TOP),
                                        },
                                    .scroll =
                                        {
                                            .scrollAxis = Amethyst::ScrollAxis::Y,
                                            .scrollBarVisibility = Amethyst::ScrollBarVisibility::AUTO,
                                            .canvasSize = Amethyst::UDim2(glm::vec2(1.0f, 0.0f), glm::vec2(0.0f, 0.0f)),
                                        },
                                });

    m_primaryNodeConn = m_graph.onPrimaryNodeChanged().connect([this](uint32_t nodeId) {
        m_shownNode = nodeId;
        m_rebuildPending = true;
    });
    // a section cannot be rebuilt from inside its own widget's callback, so the rebuild waits for the next update
    m_nodeEditedConn = m_graph.onNodeEdited().connect([this](uint32_t nodeId) {
        if (nodeId == m_shownNode && nodeId != 0) {
            m_rebuildPending = true;
        }
    });
    m_nodeValuesConn = m_graph.onNodeValuesChanged().connect([this](uint32_t nodeId) {
        if (nodeId == m_shownNode && m_root != nullptr) {
            m_root->markSubtreeDirty();
        }
    });

    showNode(m_graph.getPrimaryNode());
}

void MaterialDetailsPanel::onUpdate(float dt)
{
    (void)dt;
    if (m_rebuildPending) {
        m_rebuildPending = false;
        showNode(m_shownNode);
    }
}

void MaterialDetailsPanel::showNode(uint32_t nodeId)
{
    m_shownNode = nodeId;

    Rapture::AMaterialInstance *instance = m_material ? m_material->as<Rapture::AMaterialInstance>() : nullptr;
    m_sections->refresh([this, nodeId, instance]() {
        // a node's rows depend on its type and wires, so its section is built anew rather than reused
        m_sections->ensure<GraphNodeSection>(false, m_graph, nodeId);
        m_sections->ensure<GraphNodeSection>(nodeId != 0, m_graph, nodeId);
        if (instance != nullptr) {
            m_sections->ensure<MaterialSection>(nodeId == 0, instance->material());
        }
    });
}

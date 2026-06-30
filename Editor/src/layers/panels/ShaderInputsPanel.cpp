#include "ShaderInputsPanel.h"

#include "layers/panels/components/color_field.h"
#include "layers/panels/components/tab_layouts.h"
#include "layers/workspaces/TextureGeneratorWorkspace.h"
#include "shaders/Shader.h"
#include "shaders/ShaderReflections.h"

#include <components/common.h>
#include <components/table.h>
#include <components/ui_scope.h>
#include <modules/color.h>

#include <cstring>
#include <limits>
#include <optional>

static constexpr float ROW_HEIGHT = 32.0f;
static constexpr float LABEL_FRAC = 0.4f;
static constexpr float LABEL_PAD = 12.0f;
static constexpr float CTRL_VPAD = 4.0f;
static constexpr float CTRL_HPAD = 8.0f;

struct ShaderInputsPanel::MemberState {
    using BaseType = Rapture::PushConstantMemberInfo::BaseType;

    BaseType baseType = BaseType::UNKNOWN;
    bool isColor = false;
    double components[4] = {};
    int64_t intValue = 0;
    std::optional<ColorField> colorField;

    void initFromMetadata(const Rapture::PushConstantMemberMetadata &meta)
    {
        if (!meta.hasDefault || meta.defaultValue.empty()) return;
        const auto &def = meta.defaultValue;
        auto get = [&](int i) -> double { return static_cast<size_t>(i) < def.size() ? static_cast<double>(def[i]) : 0.0; };
        switch (baseType) {
        case BaseType::FLOAT:
            components[0] = get(0);
            break;
        case BaseType::INT:
        case BaseType::UINT:
            intValue = static_cast<int64_t>(get(0));
            break;
        case BaseType::VEC2:
            for (int i = 0; i < 2; ++i) components[i] = get(i);
            break;
        case BaseType::VEC3:
        case BaseType::VEC4:
            for (int i = 0; i < 4; ++i) components[i] = get(i);
            break;
        default:
            break;
        }
    }

    void initFromBuffer(const uint8_t *buf, const Rapture::PushConstantMemberInfo &member)
    {
        if (buf == nullptr) return;
        const uint8_t *src = buf + member.offset;
        switch (baseType) {
        case BaseType::FLOAT: {
            float f; std::memcpy(&f, src, 4); components[0] = f; break;
        }
        case BaseType::INT: {
            int32_t v; std::memcpy(&v, src, 4); intValue = v; break;
        }
        case BaseType::UINT: {
            uint32_t v; std::memcpy(&v, src, 4); intValue = static_cast<int64_t>(v); break;
        }
        case BaseType::VEC2: {
            float v[2]; std::memcpy(v, src, 8);
            components[0] = v[0]; components[1] = v[1]; break;
        }
        case BaseType::VEC3: {
            float v[3]; std::memcpy(v, src, 12);
            for (int i = 0; i < 3; ++i) components[i] = v[i]; break;
        }
        case BaseType::VEC4: {
            float v[4]; std::memcpy(v, src, 16);
            for (int i = 0; i < 4; ++i) components[i] = v[i]; break;
        }
        default: break;
        }
    }

    void writeToBuffer(uint8_t *buf, const Rapture::PushConstantMemberInfo &member) const
    {
        if (buf == nullptr) return;
        uint8_t *dst = buf + member.offset;

        if (isColor && colorField.has_value()) {
            if (baseType == BaseType::VEC3) {
                auto c = colorField->getColor3();
                float v[3] = {c.r, c.g, c.b};
                std::memcpy(dst, v, sizeof(v));
            } else if (baseType == BaseType::VEC4) {
                auto c = colorField->getColor4();
                float v[4] = {c.r, c.g, c.b, c.a};
                std::memcpy(dst, v, sizeof(v));
            }
            return;
        }

        switch (baseType) {
        case BaseType::FLOAT: {
            float f = static_cast<float>(components[0]);
            std::memcpy(dst, &f, 4);
            break;
        }
        case BaseType::INT: {
            int32_t v = static_cast<int32_t>(intValue);
            std::memcpy(dst, &v, 4);
            break;
        }
        case BaseType::UINT: {
            uint32_t v = static_cast<uint32_t>(intValue);
            std::memcpy(dst, &v, 4);
            break;
        }
        case BaseType::VEC2: {
            float v[2] = {(float)components[0], (float)components[1]};
            std::memcpy(dst, v, 8);
            break;
        }
        case BaseType::VEC3: {
            float v[3] = {(float)components[0], (float)components[1], (float)components[2]};
            std::memcpy(dst, v, 12);
            break;
        }
        case BaseType::VEC4: {
            float v[4] = {(float)components[0], (float)components[1], (float)components[2], (float)components[3]};
            std::memcpy(dst, v, 16);
            break;
        }
        default:
            break;
        }
    }
};

void ShaderInputsPanel::applyShaderDefaults(std::vector<uint8_t> &buffer, const Rapture::Shader &shader)
{
    using BaseType = Rapture::PushConstantMemberInfo::BaseType;

    for (const auto &pc : shader.getDetailedPushConstants()) {
        for (const auto &member : pc.members) {
            const auto &meta = member.metadata;
            if (!meta.hasDefault || meta.defaultValue.empty()) continue;

            uint8_t *dst = buffer.data() + member.offset;
            const auto &def = meta.defaultValue;
            auto get = [&](int i) -> float {
                return static_cast<size_t>(i) < def.size() ? def[i] : 0.0f;
            };

            switch (member.getBaseType()) {
            case BaseType::FLOAT: { float v = get(0); std::memcpy(dst, &v, 4); break; }
            case BaseType::INT:   { int32_t v = static_cast<int32_t>(get(0)); std::memcpy(dst, &v, 4); break; }
            case BaseType::UINT:  { uint32_t v = static_cast<uint32_t>(get(0)); std::memcpy(dst, &v, 4); break; }
            case BaseType::VEC2:  { float v[2] = {get(0), get(1)}; std::memcpy(dst, v, 8); break; }
            case BaseType::VEC3:  { float v[3] = {get(0), get(1), get(2)}; std::memcpy(dst, v, 12); break; }
            case BaseType::VEC4:  { float v[4] = {get(0), get(1), get(2), get(3)}; std::memcpy(dst, v, 16); break; }
            default: break;
            }
        }
    }
}

ShaderInputsPanel::ShaderInputsPanel(Amethyst::TabBar *tabBar, const PanelServices &services) : Panel(services)
{
    auto root = std::make_unique<Amethyst::Frame>();
    m_root = root.get();
    m_rootDestroyConn = m_root->onDestroy.connect([this](Amethyst::Instance *) { m_root = nullptr; });
    m_root->name = "Shader Inputs";
    m_root->addClass("background-secondary");

    Amethyst::UIScope(*m_root).scrollingFrame(
        {
            .base = {.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
            .scroll =
                {
                    .scrollAxis = Amethyst::ScrollAxis::Y,
                    .scrollBarVisibility = Amethyst::ScrollBarVisibility::AUTO,
                    .automaticCanvasSize = Amethyst::AutomaticSize::Y,
                },
        },
        [this](Amethyst::ScrollingFrameScope &sf) { m_content = &sf.component; });

    tabBar->addTab(std::move(root), iconTabLayout("Inputs"));
}

ShaderInputsPanel::~ShaderInputsPanel()
{
    if (m_root != nullptr && m_root->parent != nullptr) {
        if (auto *tabBar = m_root->parent->as<Amethyst::TabBar>()) {
            tabBar->removeTab(m_root);
        }
    }
}

void ShaderInputsPanel::setInstance(TextureGeneratorInstance *instance, std::function<void()> onChanged)
{
    if (instance == nullptr) {
        clearInstance();
        return;
    }
    rebuild(*instance, onChanged);
}

void ShaderInputsPanel::clearInstance()
{
    if (m_content != nullptr) {
        m_content->removeAllChildren();
    }
    m_memberStates.clear();
}

void ShaderInputsPanel::rebuild(TextureGeneratorInstance &instance, const std::function<void()> &onChanged)
{
    if (m_content == nullptr) return;

    m_content->removeAllChildren();
    m_memberStates.clear();

    if (!instance.generator || !instance.generator->isValid()) return;

    auto &shader = instance.generator->getShader();
    if (!shader.isReady()) return;

    const auto &pcs = shader.getDetailedPushConstants();
    if (pcs.empty()) return;

    const auto &pcInfo = pcs[0];

    size_t visibleCount = 0;
    for (const auto &member : pcInfo.members) {
        if (!member.metadata.hidden) ++visibleCount;
    }
    if (visibleCount == 0) return;

    m_memberStates.reserve(pcInfo.members.size());
    for (const auto &member : pcInfo.members) {
        if (member.metadata.hidden) {
            m_memberStates.push_back(nullptr);
            continue;
        }
        auto state = std::make_unique<MemberState>();
        state->baseType = member.getBaseType();
        state->isColor = member.metadata.isColor;
        state->initFromBuffer(instance.buffer.data(), member);
        m_memberStates.push_back(std::move(state));
    }

    Amethyst::UIScope(*m_content)
        .table(
            {
                .style = {.backgroundTransparency = 1.0f},
                .table =
                    {
                        .rowHeight = ROW_HEIGHT,
                        .separatorMode = Amethyst::TableSeparatorMode::ROWS,
                        .separatorColor = Amethyst::Color4::fromHex(0x181818),
                        .showHeader = false,
                        .rowBackgroundColor = Amethyst::Color4(0.0f, 0.0f, 0.0f, 0.0f),
                        .rowAlternateColor = Amethyst::Color4(0.0f, 0.0f, 0.0f, 0.0f),
                    },
            },
            [&](Amethyst::TableScope &t) {
                t.column("", LABEL_FRAC, Amethyst::TableColumnSizing::FIXED);
                t.column("", 1.0f - LABEL_FRAC);

                for (size_t i = 0; i < pcInfo.members.size(); ++i) {
                    const auto &member = pcInfo.members[i];
                    if (member.metadata.hidden || m_memberStates[i] == nullptr) continue;

                    MemberState *state = m_memberStates[i].get();
                    const char *label =
                        member.metadata.displayName.empty() ? member.name.c_str() : member.metadata.displayName.c_str();
                    const auto &meta = member.metadata;

                    double dmin = meta.hasRange ? static_cast<double>(meta.minValue) : std::numeric_limits<double>::lowest();
                    double dmax = meta.hasRange ? static_cast<double>(meta.maxValue) : std::numeric_limits<double>::max();
                    double speed = meta.hasRange ? (dmax - dmin) * 0.01 : 0.1;
                    int64_t imin = meta.hasRange ? static_cast<int64_t>(meta.minValue) : std::numeric_limits<int64_t>::min();
                    int64_t imax = meta.hasRange ? static_cast<int64_t>(meta.maxValue) : std::numeric_limits<int64_t>::max();

                    auto memberCopy = member;

                    t.row([=, &instance, memberCopy = std::move(memberCopy)](Amethyst::TableRowScope &tr) mutable {
                        tr.cell([label](Amethyst::UIScope &cell) {
                            cell.textLabel({
                                .classes = {"property-field"},
                                .base = {.position = Amethyst::UDim2(0.0f, LABEL_PAD, 0.0f, 0.0f),
                                         .size = Amethyst::UDim2(1.0f, -LABEL_PAD, 1.0f, 0.0f)},
                                .text = {.textColor = Amethyst::Color4::fromHex(0xffffff9e, true),
                                         .textXAlignment = Amethyst::TextXAlignment::LEFT,
                                         .textYAlignment = Amethyst::TextYAlignment::CENTER},
                                .label = std::string(label),
                            });
                        });

                        tr.cell([=, &instance](Amethyst::UIScope &cell) mutable {
                            using BT = Rapture::PushConstantMemberInfo::BaseType;

                            auto notify = [state, &instance, memberCopy, onChanged]() {
                                state->writeToBuffer(instance.buffer.data(), memberCopy);
                                if (onChanged) onChanged();
                            };

                            const Amethyst::BaseProperties s_fullCtrl = {
                                .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                                .position = Amethyst::UDim2(0.0f, CTRL_HPAD, 0.5f, 0.0f),
                                .size = Amethyst::UDim2(1.0f, -2.0f * CTRL_HPAD, 1.0f, -2.0f * CTRL_VPAD),
                            };

                            switch (state->baseType) {
                            case BT::FLOAT:
                                cell.dragFloat({.classes = {"generic-input-field"},
                                                .base = s_fullCtrl,
                                                .speed = speed,
                                                .min = dmin,
                                                .max = dmax,
                                                .value = &state->components[0]},
                                               [notify](Amethyst::DragFloatScope &d) {
                                                   d.component.onValueChanged = [notify](double) { notify(); };
                                               });
                                break;

                            case BT::INT:
                            case BT::UINT:
                                cell.dragInt({.classes = {"generic-input-field"},
                                              .base = s_fullCtrl,
                                              .speed = 1,
                                              .min = imin,
                                              .max = imax,
                                              .value = &state->intValue},
                                             [notify](Amethyst::DragIntScope &d) {
                                                 d.component.onValueChanged = [notify](int64_t) { notify(); };
                                             });
                                break;

                            case BT::VEC2: {
                                const float w = 0.5f;
                                for (int axis = 0; axis < 2; ++axis) {
                                    cell.dragFloat(
                                        {.classes = {"generic-input-field"},
                                         .base = {.anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                                                  .position = Amethyst::UDim2(axis * w, axis == 0 ? CTRL_HPAD : 2.0f, 0.5f, 0.0f),
                                                  .size = Amethyst::UDim2(w, axis == 1 ? -2.0f - CTRL_HPAD : -2.0f, 1.0f,
                                                                          -2.0f * CTRL_VPAD)},
                                         .speed = speed,
                                         .min = dmin,
                                         .max = dmax,
                                         .value = &state->components[axis]},
                                        [notify](Amethyst::DragFloatScope &d) {
                                            d.component.onValueChanged = [notify](double) { notify(); };
                                        });
                                }
                                break;
                            }

                            case BT::VEC3:
                                if (state->isColor) {
                                    cell.frame({.base = s_fullCtrl, .style = {.backgroundTransparency = 1.0f}},
                                               [state, notify](Amethyst::FrameScope &wrap) {
                                                   state->colorField.emplace(
                                                       wrap,
                                                       Amethyst::Color3(static_cast<float>(state->components[0]),
                                                                        static_cast<float>(state->components[1]),
                                                                        static_cast<float>(state->components[2])),
                                                       std::vector<std::string>{"generic-input-field"});
                                                   state->colorField->onColorChanged = [state, notify](const Amethyst::Color4 &c) {
                                                       state->components[0] = c.r;
                                                       state->components[1] = c.g;
                                                       state->components[2] = c.b;
                                                       notify();
                                                   };
                                               });
                                } else {
                                    const float w = 1.0f / 3.0f;
                                    for (int axis = 0; axis < 3; ++axis) {
                                        cell.dragFloat({.classes = {"generic-input-field"},
                                                        .base = {.anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                                                                 .position = Amethyst::UDim2(axis * w, axis == 0 ? CTRL_HPAD : 2.0f,
                                                                                             0.5f, 0.0f),
                                                                 .size = Amethyst::UDim2(w, axis == 2 ? -2.0f - CTRL_HPAD : -2.0f,
                                                                                         1.0f, -2.0f * CTRL_VPAD)},
                                                        .speed = speed,
                                                        .min = dmin,
                                                        .max = dmax,
                                                        .value = &state->components[axis]},
                                                       [notify](Amethyst::DragFloatScope &d) {
                                                           d.component.onValueChanged = [notify](double) { notify(); };
                                                       });
                                    }
                                }
                                break;

                            case BT::VEC4:
                                if (state->isColor) {
                                    cell.frame({.base = s_fullCtrl, .style = {.backgroundTransparency = 1.0f}},
                                               [state, notify](Amethyst::FrameScope &wrap) {
                                                   state->colorField.emplace(
                                                       wrap,
                                                       Amethyst::Color4(static_cast<float>(state->components[0]),
                                                                        static_cast<float>(state->components[1]),
                                                                        static_cast<float>(state->components[2]),
                                                                        static_cast<float>(state->components[3])),
                                                       std::vector<std::string>{"generic-input-field"});
                                                   state->colorField->onColorChanged = [state, notify](const Amethyst::Color4 &c) {
                                                       state->components[0] = c.r;
                                                       state->components[1] = c.g;
                                                       state->components[2] = c.b;
                                                       state->components[3] = c.a;
                                                       notify();
                                                   };
                                               });
                                } else {
                                    const float w = 0.25f;
                                    for (int axis = 0; axis < 4; ++axis) {
                                        cell.dragFloat({.classes = {"generic-input-field"},
                                                        .base = {.anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                                                                 .position = Amethyst::UDim2(axis * w, axis == 0 ? CTRL_HPAD : 2.0f,
                                                                                             0.5f, 0.0f),
                                                                 .size = Amethyst::UDim2(w, axis == 3 ? -2.0f - CTRL_HPAD : -2.0f,
                                                                                         1.0f, -2.0f * CTRL_VPAD)},
                                                        .speed = speed,
                                                        .min = dmin,
                                                        .max = dmax,
                                                        .value = &state->components[axis]},
                                                       [notify](Amethyst::DragFloatScope &d) {
                                                           d.component.onValueChanged = [notify](double) { notify(); };
                                                       });
                                    }
                                }
                                break;

                            default:
                                break;
                            }
                        });
                    });
                }

                t.component.setBaseProperties(
                    {.size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, ROW_HEIGHT * static_cast<float>(visibleCount))});
            });
}

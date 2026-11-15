#include "instance_picker.h"

#include "EntitySelection.h"
#include "Icons.h"

#include "core/utils/TypeInfo.h"
#include "scene/Scene.h"
#include "scene/instances/SceneObject.h"

#include <string>
#include <vector>

static constexpr float FACE_PADDING = 4.0f;
static constexpr float ICON_SIZE = 12.0f;
static constexpr float ICON_GAP = 6.0f;

static constexpr const char *EMPTY_TEXT = "None";
static constexpr const char *PICKING_TEXT = "Click an object";

static const std::vector<std::string> CLASSES = {"property-input-field"};

InstancePicker::InstancePicker(Amethyst::UIScope &parent, const Rapture::TypeInfo &type) : m_type(&type)
{
    buildFace(parent);
    applySelection();
}

void InstancePicker::setSubject(EntitySelection *selection, Rapture::Scene *scene)
{
    if (m_selection != selection) {
        cancelPick();
    }

    m_selection = selection;
    m_scene = scene;
}

InstancePicker::~InstancePicker()
{
    cancelPick();
}

void InstancePicker::setInstance(Rapture::SceneObject *instance)
{
    m_selectedDestroyed.disconnect();
    m_selected = instance;

    if (m_selected != nullptr) {
        m_selectedDestroyed = m_selected->onDestroy.connect([this](Rapture::Instance *) {
            m_selected = nullptr;
            applySelection();
        });
    }

    applySelection();
}

void InstancePicker::armPick()
{
    if (m_selection == nullptr || m_scene == nullptr) {
        return;
    }

    m_isPicking = true;
    applySelection();

    m_selection->requestPick([this](Rapture::ecs::EntityAccessor entity) {
        m_isPicking = false;

        Rapture::SceneObject *picked = entity.isValid() ? m_scene->instanceFor(entity.getEntity()) : nullptr;
        if (picked == nullptr || !picked->type().isA(*m_type)) {
            applySelection();
            return;
        }

        takeInstance(picked);
    });
}

void InstancePicker::cancelPick()
{
    if (!m_isPicking) {
        return;
    }

    m_isPicking = false;
    m_selection->cancelPick();
    applySelection();
}

void InstancePicker::takeInstance(Rapture::SceneObject *instance)
{
    setInstance(instance);

    if (onInstanceSelected) {
        onInstanceSelected(instance);
    }
}

void InstancePicker::buildFace(Amethyst::UIScope &parent)
{
    parent.frame(
        {
            .classes = CLASSES,
            .base = {.interactable = true, .size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
        },
        [this](Amethyst::FrameScope &f) {
            m_root = &f.component;

            f.imageLabel(
                {
                    .classes = CLASSES,
                    .base =
                        {
                            .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                            .interactable = false,
                            .position = Amethyst::UDim2(0.0f, FACE_PADDING, 0.5f, 0.0f),
                            .size = Amethyst::UDim2::fromOffset(ICON_SIZE, ICON_SIZE),
                        },
                    .style = {.backgroundTransparency = 1.0f, .borderPixelSize = 0.0f},
                    .svg = Icons::SVG_LINK,
                },
                [this](Amethyst::ImageLabelScope &il) { m_icon = &il.component; });

            float textLeft = FACE_PADDING + ICON_SIZE + ICON_GAP;

            f.textLabel(
                {
                    .classes = CLASSES,
                    .base =
                        {
                            .interactable = false,
                            .position = Amethyst::UDim2(0.0f, textLeft, 0.0f, 0.0f),
                            .size = Amethyst::UDim2(1.0f, -(textLeft + FACE_PADDING), 1.0f, 0.0f),
                        },
                    .style = {.backgroundTransparency = 1.0f, .borderPixelSize = 0.0f},
                },
                [this](Amethyst::TextLabelScope &tl) { m_label = &tl.component; });

            m_root->track(m_root->onInputBeganCb.connect([this](const Amethyst::InputObject &io) {
                if (io.type == Amethyst::InputType::MOUSE_BUTTON_2) {
                    takeInstance(nullptr);
                    return;
                }
                if (io.type != Amethyst::InputType::MOUSE_BUTTON_1) {
                    return;
                }

                if (m_isPicking) {
                    cancelPick();
                } else {
                    armPick();
                }
            }));
        });
}

void InstancePicker::applySelection()
{
    if (m_label == nullptr) {
        return;
    }

    if (m_isPicking) {
        m_label->setText(PICKING_TEXT);
        return;
    }

    m_label->setText(m_selected != nullptr ? std::string(m_selected->name()) : EMPTY_TEXT);
}

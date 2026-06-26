#include "ImportPanel.h"

#include "loaders/gltf/glTFLoader.h"
#include "logging/Log.h"
#include "scenes/Scene.h"
#include "window_context/Application.h"

#include <components/extensions/ui_drag_detector.h>

#define COL_BG     Amethyst::Color3::fromHex(0x282828)
#define COL_TITLE  Amethyst::Color3::fromHex(0x202020)
#define COL_BORDER Amethyst::Color3::fromHex(0x3a3a3a)
#define COL_BTN    Amethyst::Color3::fromHex(0x2e2e2e)
#define COL_TEXT       Amethyst::Color4(0.92f, 0.92f, 0.92f, 1.0f)
#define COL_TEXT_MUTED Amethyst::Color4(1.0f, 1.0f, 1.0f, 0.62f)

static constexpr float PANEL_WIDTH = 380.0f;
static constexpr float PANEL_HEIGHT = 150.0f;
static constexpr float TITLE_HEIGHT = 36.0f;
static constexpr float BTN_WIDTH = 92.0f;
static constexpr float BTN_HEIGHT = 32.0f;

ImportPanel::ImportPanel(Amethyst::Window &window, const std::filesystem::path &path) : m_path(path)
{
    m_popup = window.add<Amethyst::Popup>();
    m_popupDestroyConn = m_popup->onDestroy.connect([this](Amethyst::Instance *) { m_popup = nullptr; });

    build();

    Amethyst::vec2 size(PANEL_WIDTH, PANEL_HEIGHT);
    m_popup->openAt((window.absoluteSize - size) * 0.5f);

    m_tick = window.registerTick([this](float) {
        if (m_importRequested) {
            m_importRequested = false;
            doImport();
            m_closeRequested = true;
        }
        if (m_closeRequested) {
            close();
        }
    });
}

ImportPanel::~ImportPanel()
{
    m_tick.unregister();
    if (m_popup != nullptr && m_popup->parent != nullptr) {
        m_popup->parent->removeChild(m_popup);
    }
}

void ImportPanel::build()
{
    m_popup->name = "Import Panel";
    m_popup->closeOnClickOutside = false;
    m_popup->setBaseProperties({.size = Amethyst::UDim2::fromOffset(PANEL_WIDTH, PANEL_HEIGHT)});
    m_popup->setBaseStyleProperties(
        {.backgroundColor = COL_BG, .borderPixelSize = 1.0f, .borderColor = COL_BORDER, .cornerRadius = 6.0f});

    Amethyst::UIScope root(*m_popup);

    // Title bar is non-interactable so presses fall through to the popup's drag detector.
    root.frame(
        {
            .base = {.interactable = false, .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, TITLE_HEIGHT)},
            .style = {.backgroundColor = COL_TITLE, .cornerRadius = 6.0f},
        },
        [this](Amethyst::FrameScope &bar) {
            m_titleBar = &bar.component;
            bar.textLabel({
                .base =
                    {
                        .interactable = false,
                        .position = Amethyst::UDim2(0.0f, 14.0f, 0.0f, 0.0f),
                        .size = Amethyst::UDim2(1.0f, -28.0f, 1.0f, 0.0f),
                    },
                .style = {.backgroundTransparency = 1.0f},
                .text =
                    {
                        .fontSize = 13.0f,
                        .textColor = COL_TEXT,
                        .textXAlignment = Amethyst::TextXAlignment::LEFT,
                        .textYAlignment = Amethyst::TextYAlignment::CENTER,
                        .textTruncate = Amethyst::TextTruncate::AT_END,
                    },
                .label = "Import: " + m_path.filename().string(),
            });
        });

    const Amethyst::TextStyleProperties btnText{
        .fontSize = 12.0f,
        .textColor = COL_TEXT,
        .textXAlignment = Amethyst::TextXAlignment::CENTER,
        .textYAlignment = Amethyst::TextYAlignment::CENTER,
    };

    root.textButton(
        {
            .base =
                {
                    .anchorPoint = Amethyst::vec2(1.0f, 1.0f),
                    .position = Amethyst::UDim2(1.0f, -(12.0f + BTN_WIDTH + 8.0f), 1.0f, -12.0f),
                    .size = Amethyst::UDim2::fromOffset(BTN_WIDTH, BTN_HEIGHT),
                },
            .style = {.backgroundColor = COL_BTN, .cornerRadius = 4.0f},
            .text = {.fontSize = 12.0f,
                     .textColor = COL_TEXT_MUTED,
                     .textXAlignment = Amethyst::TextXAlignment::CENTER,
                     .textYAlignment = Amethyst::TextYAlignment::CENTER},
            .label = "Cancel",
        },
        [this](Amethyst::TextButtonScope &b) {
            b.component.onMouseButton1ClickCb = [this]() {
                m_closeRequested = true;
                return Amethyst::EventResult::CONSUMED;
            };
        });

    root.textButton(
        {
            .classes = {"primary"},
            .base =
                {
                    .anchorPoint = Amethyst::vec2(1.0f, 1.0f),
                    .position = Amethyst::UDim2(1.0f, -12.0f, 1.0f, -12.0f),
                    .size = Amethyst::UDim2::fromOffset(BTN_WIDTH, BTN_HEIGHT),
                },
            .style = {.cornerRadius = 4.0f},
            .text = btnText,
            .label = "Import",
        },
        [this](Amethyst::TextButtonScope &b) {
            b.component.onMouseButton1ClickCb = [this]() {
                m_importRequested = true;
                return Amethyst::EventResult::CONSUMED;
            };
        });

    m_popup->addExtension<Amethyst::UIDragDetector>(m_titleBar);
}

void ImportPanel::doImport()
{
    Rapture::Scene *scene = Rapture::Application::getInstance().getProject().getActiveScene();
    if (scene == nullptr) {
        Rapture::RP_WARN("No active scene to import into");
        return;
    }

    Rapture::glTF2Loader loader(m_path);
    if (!loader.load(scene)) {
        Rapture::RP_WARN("Failed to import: {}", m_path.string());
        return;
    }

    // TODO: having to manually rebuild the TLAS here is shit, the loader should handle this.
    scene->buildTLAS();
}

void ImportPanel::close()
{
    m_tick.unregister();
    if (m_popup != nullptr && m_popup->parent != nullptr) {
        m_popup->parent->removeChild(m_popup);
    }
    if (onClose) {
        onClose();
    }
}

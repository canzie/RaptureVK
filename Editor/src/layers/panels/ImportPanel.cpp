#include "ImportPanel.h"

#include "asset_manager/AssetManager.h"
#include "loaders/gltf/glTFLoader.h"
#include "logging/Log.h"

#include <components/checkbox.h>
#include <components/extensions/ui_drag_detector.h>
#include <components/text_input.h>

#include <cctype>

#define COL_BG         Amethyst::Color3::fromHex(0x282828)
#define COL_TITLE      Amethyst::Color3::fromHex(0x202020)
#define COL_BORDER     Amethyst::Color3::fromHex(0x3a3a3a)
#define COL_BTN        Amethyst::Color3::fromHex(0x2e2e2e)
#define COL_TEXT       Amethyst::Color4(0.92f, 0.92f, 0.92f, 1.0f)
#define COL_TEXT_MUTED Amethyst::Color4(1.0f, 1.0f, 1.0f, 0.62f)

static constexpr float PANEL_WIDTH = 380.0f;
static constexpr float PANEL_HEIGHT = 210.0f;
static constexpr float TITLE_HEIGHT = 36.0f;
static constexpr float BTN_WIDTH = 92.0f;
static constexpr float BTN_HEIGHT = 32.0f;

ImportPanel::ImportPanel(Amethyst::Window &window, const std::filesystem::path &path, const std::filesystem::path &outputFolder)
    : m_path(path), m_outputFolder(outputFolder)
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

    const float contentX = 14.0f;

    const Amethyst::TextStylePropertiesArgs sectionText{
        .fontSize = 10.0f,
        .textColor = COL_TEXT_MUTED,
        .textXAlignment = Amethyst::TextXAlignment::LEFT,
        .textYAlignment = Amethyst::TextYAlignment::CENTER,
    };

    // Source file (read-only)
    root.textLabel({
        .base = {.interactable = false,
                 .position = Amethyst::UDim2(0.0f, contentX, 0.0f, TITLE_HEIGHT + 12.0f),
                 .size = Amethyst::UDim2(1.0f, -2.0f * contentX, 0.0f, 12.0f)},
        .style = {.backgroundTransparency = 1.0f},
        .text = sectionText,
        .label = "Source",
    });
    root.textLabel({
        .base = {.interactable = false,
                 .position = Amethyst::UDim2(0.0f, contentX, 0.0f, TITLE_HEIGHT + 26.0f),
                 .size = Amethyst::UDim2(1.0f, -2.0f * contentX, 0.0f, 16.0f)},
        .style = {.backgroundTransparency = 1.0f},
        .text = {.fontSize = 12.0f,
                 .textColor = COL_TEXT,
                 .textXAlignment = Amethyst::TextXAlignment::LEFT,
                 .textYAlignment = Amethyst::TextYAlignment::CENTER,
                 .textTruncate = Amethyst::TextTruncate::AT_END},
        .label = m_path.string(),
    });

    // Output location + name (editable)
    root.textLabel({
        .base = {.interactable = false,
                 .position = Amethyst::UDim2(0.0f, contentX, 0.0f, TITLE_HEIGHT + 52.0f),
                 .size = Amethyst::UDim2(1.0f, -2.0f * contentX, 0.0f, 12.0f)},
        .style = {.backgroundTransparency = 1.0f},
        .text = sectionText,
        .label = "Output",
    });
    root.frame(
        {
            .base = {.position = Amethyst::UDim2(0.0f, contentX, 0.0f, TITLE_HEIGHT + 66.0f),
                     .size = Amethyst::UDim2(1.0f, -2.0f * contentX, 0.0f, 26.0f)},
            .style = {.backgroundColor = COL_BTN, .borderPixelSize = 1.0f, .borderColor = COL_BORDER, .cornerRadius = 4.0f},
        },
        [this](Amethyst::FrameScope &field) {
            field.textInput(
                {
                    .base = {.position = Amethyst::UDim2(0.0f, 8.0f, 0.0f, 0.0f),
                             .size = Amethyst::UDim2(1.0f, -16.0f, 1.0f, 0.0f)},
                    .style = {.backgroundTransparency = 1.0f},
                    .textInput = {.text = {.fontSize = 12.0f,
                                           .textColor = COL_TEXT,
                                           .textYAlignment = Amethyst::TextYAlignment::CENTER}},
                    .placeholder = "name",
                },
                [this](Amethyst::TextInputScope &ti) {
                    m_outputInput = &ti.component;
                    m_outputInput->setText(m_path.stem().string());
                });
        });

    root.checkbox(
        {
            .base = {.position = Amethyst::UDim2(0.0f, contentX, 0.0f, TITLE_HEIGHT + 102.0f),
                     .size = Amethyst::UDim2::fromOffset(16.0f, 16.0f)},
            .value = &m_createSubfolder,
        },
        [](Amethyst::CheckboxScope &) {});
    root.textLabel({
        .base = {.interactable = false,
                 .position = Amethyst::UDim2(0.0f, contentX + 24.0f, 0.0f, TITLE_HEIGHT + 100.0f),
                 .size = Amethyst::UDim2(1.0f, -(2.0f * contentX + 24.0f), 0.0f, 20.0f)},
        .style = {.backgroundTransparency = 1.0f},
        .text = {.fontSize = 11.0f,
                 .textColor = COL_TEXT,
                 .textXAlignment = Amethyst::TextXAlignment::LEFT,
                 .textYAlignment = Amethyst::TextYAlignment::CENTER},
        .label = "Place in its own subfolder",
    });

    const Amethyst::TextStylePropertiesArgs btnText{
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
    std::string name = m_outputInput != nullptr ? m_outputInput->getText() : std::string{};
    if (name.empty()) {
        name = m_path.stem().string();
    }

    std::string extension = m_path.extension().string();
    for (char &c : extension) {
        c = static_cast<char>(std::tolower(c));
    }

    std::filesystem::path output = m_createSubfolder ? m_outputFolder / name : m_outputFolder;

    if (extension == ".gltf" || extension == ".glb") {
        Rapture::glTF2Loader loader(m_path, output, name);
        if (!loader.load()) {
            RP_WARN("Failed to import: {}", m_path.string());
            return;
        }
    } else {
        Rapture::AssetImportFileRequest request{.source = m_path, .output = output, .name = name};
        if (!Rapture::AssetManager::importAsset(request)) {
            RP_WARN("Failed to import: {}", m_path.string());
            return;
        }
    }

    RP_INFO("Imported '{}'", name);
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

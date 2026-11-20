#include "SearchField.h"

#include "Icons.h"

#include <components/common.h>
#include <components/image_label.h>
#include <components/input_events.h>
#include <components/text_input.h>
#include <components/ui_scope.h>

#include <utility>

static constexpr float ICON_LEFT = 8.0f;
static constexpr float ICON_SIZE = 13.0f;
static constexpr float ICON_INPUT_GAP = 7.0f;
static constexpr float INPUT_LEFT = ICON_LEFT + ICON_SIZE + ICON_INPUT_GAP;

SearchField::SearchField(std::string_view placeholder)
{
    addClass("search-field");

    Amethyst::UIScope scope(*this);
    scope.imageLabel({
        .classes = {"search-field-icon"},
        .base =
            {
                .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                .interactable = false,
                .position = Amethyst::UDim2(0.0f, ICON_LEFT, 0.5f, 0.0f),
                .size = Amethyst::UDim2::fromOffset(ICON_SIZE, ICON_SIZE),
            },
        .svg = Icons::SVG_SEARCH,
    });
    scope.textInput(
        {
            .classes = {"search-field-input"},
            .base =
                {
                    .position = Amethyst::UDim2::fromOffset(INPUT_LEFT, 0.0f),
                    .size = Amethyst::UDim2(1.0f, -INPUT_LEFT, 1.0f, 0.0f),
                },
            .placeholder = std::string(placeholder),
        },
        [this](Amethyst::TextInputScope &ti) { m_input = &ti.component; });

    // the field draws the border, so it shows the focus its input holds
    m_input->onFocusGained = [this]() { setGuiState(static_cast<uint16_t>(getGuiState() | Amethyst::GUI_STATE_FOCUSED)); };
    m_input->onFocusLost = [this]() { setGuiState(static_cast<uint16_t>(getGuiState() & ~Amethyst::GUI_STATE_FOCUSED)); };
}

void SearchField::setOnTextChanged(std::function<void(const std::string &)> callback)
{
    m_input->onTextChanged = std::move(callback);
}

void SearchField::setOnEnterPressed(std::function<void()> callback)
{
    m_input->onEnterPressed = std::move(callback);
}

void SearchField::setOnEscapePressed(std::function<void()> callback)
{
    m_input->interceptKey = [callback = std::move(callback)](const Amethyst::KeyEvent &event) {
        if (event.action == Amethyst::KEY_ACTION_RELEASE || event.key != Amethyst::KEY_ESCAPE) {
            return false;
        }
        callback();
        return true;
    };
}

void SearchField::clear()
{
    m_input->setText({});
}

void SearchField::focus()
{
    m_input->focus();
}

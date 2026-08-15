#include "segmented_control.h"

#include <components/extensions/ui_list_layout.h>
#include <components/ui_scope.h>

static constexpr float SEGMENT_GAP = 8.0f;
static constexpr float SEGMENT_TEXT_PAD = 10.0f;

SegmentedControl::SegmentedControl(const std::vector<std::string> &options, SegmentedSelection selection)
    : m_selection(selection)
{
    m_selected.assign(options.size(), false);

    auto *layout = addExtension<Amethyst::UIListLayout>();
    layout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
    layout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
    layout->horizontalAlignment = Amethyst::HorizontalAlignment::ALIGN_LEFT;
    layout->innerPadding = Amethyst::UDim::fromOffset(SEGMENT_GAP);

    Amethyst::UIScope scope(*this);
    for (int32_t i = 0; i < static_cast<int32_t>(options.size()); ++i) {
        scope.textButton(
            {
                .classes = {"segment-control"},
                .base = {.size = Amethyst::UDim2(0.0f, 0.0f, 1.0f, 0.0f)},
                .label = options[i],
            },
            [this, i](Amethyst::TextButtonScope &b) {
                m_buttons.push_back(&b.component);
                b.component.onMouseButton1ClickCb = [this, i]() {
                    if (m_selection == SegmentedSelection::MULTIPLE && isSelected(i)) {
                        deselect(i);
                    } else {
                        select(i);
                    }
                    return Amethyst::EventResult::CONSUMED;
                };
            });
    }
}

void SegmentedControl::draw(Amethyst::DrawContext &ctx)
{
    fitToLabels();
    Amethyst::Frame::draw(ctx);
}

void SegmentedControl::fitToLabels()
{
    if (m_fitted) {
        return;
    }

    // a button only knows how wide its label is once it has been through a draw, so the fit waits
    // for the first one that has measured
    for (Amethyst::TextButton *button : m_buttons) {
        if (button->getTextSize().x <= 0.0f) {
            return;
        }
    }

    for (Amethyst::TextButton *button : m_buttons) {
        button->setBaseProperties({
            .size = Amethyst::UDim2(0.0f, button->getTextSize().x + 2.0f * SEGMENT_TEXT_PAD, 1.0f, 0.0f),
        });
    }

    m_fitted = true;
}

void SegmentedControl::select(int32_t index)
{
    if (index < 0 || index >= static_cast<int32_t>(m_selected.size()) || m_selected[index]) {
        return;
    }

    if (m_selection == SegmentedSelection::SINGLE) {
        for (int32_t i = 0; i < static_cast<int32_t>(m_selected.size()); ++i) {
            if (m_selected[i]) {
                deselect(i);
            }
        }
    }

    m_selected[index] = true;
    applyState(index);

    if (onChanged != nullptr) {
        onChanged(index, true);
    }
}

void SegmentedControl::deselect(int32_t index)
{
    if (index < 0 || index >= static_cast<int32_t>(m_selected.size()) || !m_selected[index]) {
        return;
    }

    m_selected[index] = false;
    applyState(index);

    if (onChanged != nullptr) {
        onChanged(index, false);
    }
}

void SegmentedControl::clear()
{
    for (int32_t i = 0; i < static_cast<int32_t>(m_selected.size()); ++i) {
        deselect(i);
    }
}

bool SegmentedControl::isSelected(int32_t index) const
{
    return index >= 0 && index < static_cast<int32_t>(m_selected.size()) && m_selected[index];
}

void SegmentedControl::applyState(int32_t index)
{
    if (index < 0 || index >= static_cast<int32_t>(m_buttons.size())) {
        return;
    }

    Amethyst::TextButton *button = m_buttons[index];
    const uint16_t state = button->getGuiState();
    button->setGuiState(
        static_cast<uint16_t>(m_selected[index] ? (state | Amethyst::GUI_STATE_ACTIVE) : (state & ~Amethyst::GUI_STATE_ACTIVE)));
}

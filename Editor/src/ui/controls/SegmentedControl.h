#ifndef RAPTURE__SEGMENTED_CONTROL_H
#define RAPTURE__SEGMENTED_CONTROL_H

#include <amethyst/Amethyst.h>
#include <components/frame.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/**
 * @brief How many of a segmented control's options may be selected at once.
 */
enum class SegmentedSelection {
    SINGLE,
    MULTIPLE
};

/**
 * @brief Editor segmented control: a row of joined buttons, one per option, each as wide as the
 * option it names, where a selected option is the one wearing the active state.
 */
class SegmentedControl : public Amethyst::Frame {
  public:
    SegmentedControl(const std::vector<std::string> &options, SegmentedSelection selection);

    void draw(Amethyst::DrawContext &ctx) override;

    /**
     * @brief Selects an option, which in single selection deselects the one that held it
     * @param index The option to select
     */
    void select(int32_t index);

    /**
     * @brief Deselects an option
     * @param index The option to deselect
     */
    void deselect(int32_t index);

    /**
     * @brief Deselects every option
     */
    void clear();

    /**
     * @brief Whether an option is selected
     * @param index The option to test
     * @return True if it is selected
     */
    bool isSelected(int32_t index) const;

    /// Fired with the option that changed and whether it ended up selected
    std::function<void(int32_t, bool)> onChanged;

  private:
    /**
     * @brief Lays the options out left to right, each as wide as its own label
     */
    void fitToLabels();

    /**
     * @brief Puts an option's button into the state its selected flag says it is in
     * @param index The option to restate
     */
    void applyState(int32_t index);

    std::vector<Amethyst::TextButton *> m_buttons;
    std::vector<bool> m_selected;
    SegmentedSelection m_selection = SegmentedSelection::SINGLE;
    bool m_fitted = false;
};

#endif // RAPTURE__SEGMENTED_CONTROL_H

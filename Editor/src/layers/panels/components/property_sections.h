#ifndef RAPTURE__PROPERTY_SECTIONS_H
#define RAPTURE__PROPERTY_SECTIONS_H

#include <amethyst/Amethyst.h>
#include <components/table.h>
#include <components/ui_scope.h>

#include "layers/panels/components/asset_picker.h"
#include "layers/panels/components/color_field.h"

#include <concepts>
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <vector>

/**
 * @brief A single collapsible section in a property list, editing one facet of a subject.
 *
 * Each section owns its own widgets and scratch value buffers. The list creates one per facet the
 * subject has, reuses it while that facet stays present, and destroys it when the facet is gone.
 */
class PropertySection {
  public:
    virtual ~PropertySection() = default;

    /**
     * @brief Section title shown in the collapsible header.
     */
    virtual const char *title() const = 0;

    /**
     * @brief SVG icon shown next to the title, or empty for none.
     */
    virtual const char *icon() const = 0;

    /**
     * @brief Current expanded body height in pixels, may vary with state.
     */
    virtual float bodyHeight() const { return m_bodyHeight; }

    /**
     * @brief Builds the widgets into the section body once, on creation.
     * @param ch Scope of the collapsible header this section fills.
     */
    virtual void buildBody(Amethyst::CollapsibleHeaderScope &ch) = 0;

    /**
     * @brief Pushes the subject's data into the bound widget buffers.
     */
    virtual void sync() = 0;

  protected:
    /**
     * @brief Builds the two-column table this section's rows live in, and takes its body height from it.
     * @param ch Scope of the collapsible header the table fills.
     * @param fn Callback that adds the rows.
     */
    void fieldTable(Amethyst::CollapsibleHeaderScope &ch, const std::function<void(Amethyst::TableScope &)> &fn);

    /**
     * @brief Adds a row of three drag fields, one per axis.
     * @param t Scope of the table the row is added to.
     * @param label Text shown in the row's label column.
     * @param values The three values the fields are bound to.
     * @param speed Units moved per pixel dragged.
     * @param min Lower bound of every axis.
     * @param max Upper bound of every axis.
     * @param onChanged Called after any axis changes.
     */
    void rowVec3(Amethyst::TableScope &t, std::string_view label, double (&values)[3], double speed, double min, double max,
                 const std::function<void(void)> &onChanged);

    /**
     * @brief Adds a row holding a single slider.
     * @param t Scope of the table the row is added to.
     * @param label Text shown in the row's label column.
     * @param value The value the slider is bound to.
     * @param min Lower bound of the slider.
     * @param max Upper bound of the slider.
     * @param onChanged Called with the new value on change.
     * @param format printf-style format for the readout, empty for the slider's default.
     */
    void rowSlider(Amethyst::TableScope &t, std::string_view label, float *value, float min, float max,
                   const std::function<void(float)> &onChanged, std::string format = {});

    /**
     * @brief Adds a row holding a single checkbox.
     * @param t Scope of the table the row is added to.
     * @param label Text shown in the row's label column.
     * @param value The value the checkbox is bound to.
     * @param onChanged Called with the new value on change.
     */
    void rowCheckbox(Amethyst::TableScope &t, std::string_view label, bool *value, const std::function<void(bool)> &onChanged);

    /**
     * @brief Adds a row holding a colour swatch and its picker.
     * @param t Scope of the table the row is added to.
     * @param label Text shown in the row's label column.
     * @param out Receives the field, which stays alive for as long as this section does.
     * @param initial Colour the field opens on.
     * @param onChanged Called with the new colour on change.
     */
    void rowColor(Amethyst::TableScope &t, std::string_view label, std::optional<ColorField> &out, const glm::vec3 &initial,
                  const std::function<void(const glm::vec3 &)> &onChanged);

    /**
     * @brief Adds a row holding an asset picker.
     * @param t Scope of the table the row is added to.
     * @param label Text shown in the row's label column.
     * @param out Receives the picker, which stays alive for as long as this section does.
     * @param config What the picker may select and how it presents it.
     * @param onSelected Called with the picked asset.
     */
    void rowAssetPicker(Amethyst::TableScope &t, std::string_view label, std::optional<AssetPicker> &out, AssetPickerConfig config,
                        const std::function<void(Rapture::AssetHandle)> &onSelected);

    /**
     * @brief Adds a row holding a dropdown.
     * @param t Scope of the table the row is added to.
     * @param label Text shown in the row's label column.
     * @param current Text the dropdown opens on.
     * @param options The selectable entries, in order.
     * @param onSelect Called with the index of the picked entry.
     * @return The dropdown, so its text can be updated as the subject changes.
     */
    Amethyst::Dropdown *rowDropdown(Amethyst::TableScope &t, std::string_view label, std::string_view current,
                                    const std::vector<std::string> &options, const std::function<void(int)> &onSelect);

    /**
     * @brief Adds a row holding a single drag field.
     * @param t Scope of the table the row is added to.
     * @param label Text shown in the row's label column.
     * @param value The value the field is bound to.
     * @param speed Units moved per pixel dragged.
     * @param min Lower bound of the field.
     * @param max Upper bound of the field.
     * @param format printf-style format for the readout, empty for the field's default.
     * @param onChanged Called with the new value on change.
     */
    void rowDragFloat(Amethyst::TableScope &t, std::string_view label, double *value, double speed, double min, double max,
                      std::string format, const std::function<void(double)> &onChanged);

  public:
    Amethyst::CollapsibleHeader *header = nullptr;

  protected:
    float m_bodyHeight = 0.0f;
};

/**
 * @brief A scrolling stack of collapsible property sections.
 *
 * The list owns the scroll view its sections are built into, and takes which sections are live from
 * a run of ensure calls between beginRefresh and relayout.
 */
class PropertySectionList {
  public:
    /**
     * @brief Builds the scroll view the sections stack into.
     * @param parent Frame the view is added to.
     * @param props Placement and scroll behaviour of the view.
     */
    PropertySectionList(Amethyst::Frame &parent, Amethyst::ScrollingFrameProperties props);

    PropertySectionList(const PropertySectionList &) = delete;
    PropertySectionList &operator=(const PropertySectionList &) = delete;

    /**
     * @brief Rebuilds which sections are live from the subject, then stacks them.
     * @param fn Callback that ensures one section per facet the subject has.
     */
    void refresh(const std::function<void()> &fn);

    /**
     * @brief Creates the section for T if its facet is present and it is missing, reuses it if it
     * already exists, or destroys it if present is false. Live sections stack in the order they are
     * ensured, so a base facet's section sits above the ones its derived facets add.
     * @tparam T A PropertySection-derived type.
     * @param present Whether the subject has the facet this section edits.
     * @return The live section, or nullptr when present is false.
     */
    template <std::derived_from<PropertySection> T>
    T *ensure(bool present)
    {
        std::type_index key(typeid(T));
        auto it = m_sections.find(key);

        if (!present) {
            if (it != m_sections.end()) {
                destroySection(*it->second);
                m_sections.erase(it);
            }
            return nullptr;
        }

        if (it == m_sections.end()) {
            auto section = std::make_unique<T>();
            buildSection(*section);
            it = m_sections.emplace(key, std::move(section)).first;
        }
        m_active.push_back(it->second.get());
        return static_cast<T *>(it->second.get());
    }

    /**
     * @brief Stacks the live sections top to bottom and fits the scroll canvas around them.
     */
    void relayout();

    /**
     * @brief Pushes the subject's data into every live section.
     */
    void sync();

    void setVisible(bool visible);

    bool empty() const { return m_active.empty(); }

  private:
    void buildSection(PropertySection &section);
    void destroySection(PropertySection &section);

  private:
    Amethyst::ScrollingFrame *m_view = nullptr;
    Amethyst::EventConnection m_viewDestroyConn;
    std::unordered_map<std::type_index, std::unique_ptr<PropertySection>> m_sections;
    std::vector<PropertySection *> m_active;
};

#endif // RAPTURE__PROPERTY_SECTIONS_H

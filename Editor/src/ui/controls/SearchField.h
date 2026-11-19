#ifndef RAPTURE__SEARCH_FIELD_H
#define RAPTURE__SEARCH_FIELD_H

#include <amethyst/Amethyst.h>
#include <components/frame.h>

#include <functional>
#include <string>
#include <string_view>

namespace Amethyst {
class TextInput;
} // namespace Amethyst

/**
 * @brief A text field with a search icon in front of it, styled by the search-field classes
 */
class SearchField : public Amethyst::Frame {
  public:
    explicit SearchField(std::string_view placeholder);

    /**
     * @brief Sets what runs whenever the search text changes
     * @param callback Called with the new text
     */
    void setOnTextChanged(std::function<void(const std::string &)> callback);

  private:
    Amethyst::TextInput *m_input = nullptr;
};

#endif // RAPTURE__SEARCH_FIELD_H

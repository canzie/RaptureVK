#ifndef RAPTURE__SEARCHBAR_H
#define RAPTURE__SEARCHBAR_H

#include <functional>
#include <string_view>

namespace Amethyst {
class Frame;
class FrameScope;
} // namespace Amethyst

struct SearchBarComponentArgs {
    std::string_view placeholder;
    bool useIcon = true;
};

std::function<void(Amethyst::FrameScope &)> searchbar(std::string_view p, std::string_view iconSvg = {});
#endif // RAPTURE__SEARCHBAR_H

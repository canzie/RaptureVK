#ifndef RAPTURE__ICONS_H
#define RAPTURE__ICONS_H

// All icons share viewBox="0 0 16 16", stroke-width=1.5, stroke-linecap/linejoin=round.
// Filled shapes override fill/stroke per-element.
#define ICON_SVG(body)                                                                                                       \
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 16 16\" fill=\"none\" stroke=\"#ffffff\" stroke-width=\"1.5\" " \
    "stroke-linecap=\"round\" stroke-linejoin=\"round\">" body "</svg>"

namespace Icons {

inline constexpr const char *SVG_SEARCH = ICON_SVG("<circle cx=\"7\" cy=\"7\" r=\"4.2\"/>"
                                                   "<path d=\"M10.2 10.2L13 13\"/>");

inline constexpr const char *SVG_PLUS = ICON_SVG("<path d=\"M8 3v10M3 8h10\"/>");

inline constexpr const char *SVG_MINUS = ICON_SVG("<path d=\"M3 8h10\"/>");

inline constexpr const char *SVG_X = ICON_SVG("<path d=\"M4 4l8 8M12 4l-8 8\"/>");

inline constexpr const char *SVG_MORE = ICON_SVG("<g fill=\"#ffffff\" stroke=\"none\">"
                                                 "<circle cx=\"3.5\" cy=\"8\" r=\"1\"/>"
                                                 "<circle cx=\"8\" cy=\"8\" r=\"1\"/>"
                                                 "<circle cx=\"12.5\" cy=\"8\" r=\"1\"/>"
                                                 "</g>");

inline constexpr const char *SVG_FILTER = ICON_SVG("<path d=\"M2 3h12l-4.5 6V13l-3 1V9L2 3z\"/>");

inline constexpr const char *SVG_GRIP = ICON_SVG("<g fill=\"#ffffff\" stroke=\"none\">"
                                                 "<circle cx=\"6\" cy=\"4\" r=\"1\"/>"
                                                 "<circle cx=\"10\" cy=\"4\" r=\"1\"/>"
                                                 "<circle cx=\"6\" cy=\"8\" r=\"1\"/>"
                                                 "<circle cx=\"10\" cy=\"8\" r=\"1\"/>"
                                                 "<circle cx=\"6\" cy=\"12\" r=\"1\"/>"
                                                 "<circle cx=\"10\" cy=\"12\" r=\"1\"/>"
                                                 "</g>");

inline constexpr const char *SVG_CHECK = ICON_SVG("<path d=\"M3 8.5l3 3 7-7\"/>");

inline constexpr const char *SVG_LINK = ICON_SVG("<path d=\"M9 7l-2 2\"/>"
                                                 "<path d=\"M6.5 5L8 3.5a2.1 2.1 0 113 3L9.5 8\"/>"
                                                 "<path d=\"M9.5 11L8 12.5a2.1 2.1 0 11-3-3L6.5 8\"/>");

inline constexpr const char *SVG_PIN = ICON_SVG("<path d=\"M6 2h4l-1 4 2 2H5l2-2-1-4z\"/>"
                                                "<path d=\"M8 8v5\"/>");

inline constexpr const char *SVG_COPY = ICON_SVG("<rect x=\"5\" y=\"5\" width=\"8\" height=\"8\" rx=\"1\"/>"
                                                 "<path d=\"M3 11V4a1 1 0 011-1h7\"/>");

inline constexpr const char *SVG_RESET = ICON_SVG("<path d=\"M3.5 8a4.5 4.5 0 108-3.2\"/>"
                                                  "<path d=\"M12 2v3h-3\"/>");

inline constexpr const char *SVG_EYE = ICON_SVG("<path d=\"M1.5 8s2.5-4.5 6.5-4.5S14.5 8 14.5 8 12 12.5 8 12.5 1.5 8 1.5 8z\"/>"
                                                "<circle cx=\"8\" cy=\"8\" r=\"1.6\"/>");

inline constexpr const char *SVG_EYE_OFF = ICON_SVG("<path d=\"M1.5 8s2.5-4.5 6.5-4.5c1.4 0 2.6.5 3.6 1.2\"/>"
                                                    "<path d=\"M14.5 8s-.7 1.2-2 2.4\"/>"
                                                    "<path d=\"M5 11.4c1 .7 1.9 1.1 3 1.1 1 0 1.9-.3 2.8-.8\"/>"
                                                    "<path d=\"M2 2l12 12\"/>");

inline constexpr const char *SVG_LOCK = ICON_SVG("<rect x=\"3.5\" y=\"7\" width=\"9\" height=\"6\" rx=\"1\"/>"
                                                 "<path d=\"M5.5 7V5a2.5 2.5 0 015 0v2\"/>");

inline constexpr const char *SVG_UNLOCK = ICON_SVG("<rect x=\"3.5\" y=\"7\" width=\"9\" height=\"6\" rx=\"1\"/>"
                                                   "<path d=\"M5.5 7V5a2.5 2.5 0 014.7-1.2\"/>");

inline constexpr const char *SVG_PLAY = ICON_SVG("<path d=\"M5 3.5v9l7-4.5-7-4.5z\" fill=\"#ffffff\" stroke=\"none\"/>");

inline constexpr const char *SVG_PAUSE = ICON_SVG("<g fill=\"#ffffff\" stroke=\"none\">"
                                                  "<rect x=\"4\" y=\"3.5\" width=\"2.5\" height=\"9\" rx=\".5\"/>"
                                                  "<rect x=\"9.5\" y=\"3.5\" width=\"2.5\" height=\"9\" rx=\".5\"/>"
                                                  "</g>");

inline constexpr const char *SVG_STOP =
    ICON_SVG("<rect x=\"4\" y=\"4\" width=\"8\" height=\"8\" rx=\"1\" fill=\"#ffffff\" stroke=\"none\"/>");

inline constexpr const char *SVG_FRAME_ADVANCE = ICON_SVG("<path d=\"M5 3.5v9l5-4.5-5-4.5z\" fill=\"#ffffff\" stroke=\"none\"/>"
                                                 "<path d=\"M11 3.5v9\"/>");

inline constexpr const char *SVG_CARET_DOWN = ICON_SVG("<path d=\"M4 6l4 4 4-4\"/>");

inline constexpr const char *SVG_CARET_RIGHT = ICON_SVG("<path d=\"M6 4l4 4-4 4\"/>");

inline constexpr const char *SVG_NAV_BACK = ICON_SVG("<path d=\"M7 4L3 8l4 4\"/>"
                                                     "<path d=\"M3 8h9a1 1 0 011 1v2\"/>");

inline constexpr const char *SVG_NAV_FORWARD = ICON_SVG("<path d=\"M9 4l4 4-4 4\"/>"
                                                        "<path d=\"M13 8H4a1 1 0 00-1 1v2\"/>");

inline constexpr const char *SVG_NAV_UP = ICON_SVG("<path d=\"M8 12.5V4M4.5 7.5L8 4l3.5 3.5\"/>"
                                                   "<path d=\"M3 3.5h10\"/>");

inline constexpr const char *SVG_REFRESH = ICON_SVG("<path d=\"M3.2 8a4.8 4.8 0 018.2-3.3L13 6\"/>"
                                                    "<path d=\"M13 2.5V6h-3.5\"/>"
                                                    "<path d=\"M12.8 8a4.8 4.8 0 01-8.2 3.3L3 10\"/>"
                                                    "<path d=\"M3 13.5V10h3.5\"/>");

inline constexpr const char *SVG_CHEVRON_DOWN = ICON_SVG("<path d=\"M3.5 6L8 10l4.5-4\"/>");

inline constexpr const char *SVG_CHEVRON_UP = ICON_SVG("<path d=\"M3.5 10L8 6l4.5 4\"/>");

inline constexpr const char *SVG_CARET_SMALL = ICON_SVG("<path d=\"M4 6.5l4 3 4-3\" fill=\"#ffffff\" stroke=\"none\"/>");

inline constexpr const char *SVG_SCENE = ICON_SVG("<path d=\"M2 5l6-3 6 3-6 3-6-3z\"/>"
                                                  "<path d=\"M2 8l6 3 6-3\"/>"
                                                  "<path d=\"M2 11l6 3 6-3\"/>");

inline constexpr const char *SVG_FOLDER =
    ICON_SVG("<path d=\"M2 5a1 1 0 011-1h3l1.5 1.5H13a1 1 0 011 1V12a1 1 0 01-1 1H3a1 1 0 01-1-1V5z\"/>");

inline constexpr const char *SVG_FOLDER_PLUS =
    ICON_SVG("<path d=\"M2 5a1 1 0 011-1h3l1.5 1.5H13a1 1 0 011 1V12a1 1 0 01-1 1H3a1 1 0 01-1-1V5z\"/>"
             "<path d=\"M8 7.5v3M6.5 9h3\"/>");

inline constexpr const char *SVG_CUBE = ICON_SVG("<path d=\"M8 2l5.5 3v6L8 14 2.5 11V5L8 2z\"/>"
                                                 "<path d=\"M2.5 5L8 8l5.5-3\"/>"
                                                 "<path d=\"M8 8v6\"/>");

inline constexpr const char *SVG_MESH = ICON_SVG("<path d=\"M8 2l5.5 3v6L8 14 2.5 11V5L8 2z\"/>"
                                                 "<path d=\"M2.5 5L8 8l5.5-3M8 8v6M2.5 11L8 8 13.5 11\"/>");

inline constexpr const char *SVG_LIGHT = ICON_SVG("<path d=\"M8 2.5a4 4 0 00-2.5 7.1V11h5V9.6A4 4 0 008 2.5z\"/>"
                                                  "<path d=\"M6.5 12.5h3M7 14h2\"/>");

inline constexpr const char *SVG_CAMERA =
    ICON_SVG("<path d=\"M3 5h2l1-1.5h4L11 5h2a1 1 0 011 1v6a1 1 0 01-1 1H3a1 1 0 01-1-1V6a1 1 0 011-1z\"/>"
             "<circle cx=\"8\" cy=\"9\" r=\"2\"/>");

inline constexpr const char *SVG_AUDIO = ICON_SVG("<path d=\"M3 6h2l3-2.5v9L5 10H3a1 1 0 01-1-1V7a1 1 0 011-1z\"/>"
                                                  "<path d=\"M11 5.5a3.5 3.5 0 010 5\"/>");

inline constexpr const char *SVG_PARTICLE = ICON_SVG("<g fill=\"#ffffff\" stroke=\"none\">"
                                                     "<circle cx=\"4\" cy=\"5\" r=\"1.2\"/>"
                                                     "<circle cx=\"10\" cy=\"4\" r=\"1.2\"/>"
                                                     "<circle cx=\"8\" cy=\"9\" r=\"1.5\"/>"
                                                     "<circle cx=\"12\" cy=\"10\" r=\"1\"/>"
                                                     "<circle cx=\"4\" cy=\"11\" r=\"1\"/>"
                                                     "</g>");

inline constexpr const char *SVG_COLLIDER =
    ICON_SVG("<rect x=\"3\" y=\"3\" width=\"10\" height=\"10\" rx=\"1\" stroke-dasharray=\"2 2\"/>");

inline constexpr const char *SVG_SCRIPT =
    ICON_SVG("<path d=\"M3.5 2.5h6l3 3V13a.5.5 0 01-.5.5h-8a.5.5 0 01-.5-.5V3a.5.5 0 01.5-.5z\"/>"
             "<path d=\"M9.5 2.5V5h3\"/>"
             "<path d=\"M5.5 8h5M5.5 10h4\"/>");

inline constexpr const char *SVG_WORLD = ICON_SVG("<circle cx=\"8\" cy=\"8\" r=\"5.75\"/>"
                                                  "<path d=\"M2.25 8h11.5\"/>"
                                                  "<path d=\"M8 2.25a9 9 0 010 11.5a9 9 0 010-11.5z\"/>");

inline constexpr const char *SVG_CONTROLLER =
    ICON_SVG("<path d=\"M5.5 5.5h5a3.5 3.5 0 013.4 2.7l.6 2.4a1.6 1.6 0 01-2.8 1.4L10.6 10H5.4l-1.1 2a1.6 1.6 0 "
             "01-2.8-1.4l.6-2.4A3.5 3.5 0 015.5 5.5z\"/>"
             "<path d=\"M4.8 7.8v1.6M4 8.6h1.6\"/>"
             "<circle cx=\"11.2\" cy=\"8.6\" r=\".9\" fill=\"#ffffff\" stroke=\"none\"/>");

inline constexpr const char *SVG_GROUP = ICON_SVG("<rect x=\"2\" y=\"3\" width=\"6\" height=\"6\" rx=\".5\"/>"
                                                  "<rect x=\"8\" y=\"7\" width=\"6\" height=\"6\" rx=\".5\"/>");

inline constexpr const char *SVG_TRANSFORM = ICON_SVG("<path d=\"M2 14l4-4M2 14h3M2 14v-3\"/>"
                                                      "<path d=\"M6 10l4-4\"/>"
                                                      "<path d=\"M10 6l4-4M14 2h-3M14 2v3\"/>");

inline constexpr const char *SVG_MATERIAL = ICON_SVG("<circle cx=\"8\" cy=\"8\" r=\"5\"/>"
                                                     "<path d=\"M3 8c0-2 2-3 5-3s5 1 5 3\"/>");

inline constexpr const char *SVG_TAG = ICON_SVG("<path d=\"M3 8l5-5h5v5l-5 5-5-5z\"/>"
                                                "<circle cx=\"10\" cy=\"6\" r=\".8\" fill=\"#ffffff\"/>");

inline constexpr const char *SVG_SAVE = ICON_SVG("<path d=\"M3 3h7l3 3v7a1 1 0 01-1 1H3a1 1 0 01-1-1V4a1 1 0 011-1z\"/>"
                                                 "<path d=\"M5 3v4h6V4M5 14v-4h6v4\"/>");

inline constexpr const char *SVG_BUILD = ICON_SVG("<path d=\"M3 13L13 3\"/>"
                                                  "<path d=\"M9.5 3H13v3.5\"/>"
                                                  "<path d=\"M3 6.5V3h3.5\"/>");

inline constexpr const char *SVG_SETTINGS = ICON_SVG(
    "<g fill=\"#ffffff\" stroke=\"none\">"
    "<path fill-rule=\"evenodd\" d=\"M8 3.4A4.6 4.6 0 108 12.6 4.6 4.6 0 008 3.4Zm0 2.7A1.9 1.9 0 118 9.9 1.9 1.9 0 018 6.1Z\"/>"
    "<g>"
    "<rect x=\"6.7\" y=\"2.1\" width=\"2.6\" height=\"1.5\" rx=\"0.35\"/>"
    "<rect x=\"6.7\" y=\"12.4\" width=\"2.6\" height=\"1.5\" rx=\"0.35\"/>"
    "<rect x=\"2.1\" y=\"6.7\" width=\"1.5\" height=\"2.6\" rx=\"0.35\"/>"
    "<rect x=\"12.4\" y=\"6.7\" width=\"1.5\" height=\"2.6\" rx=\"0.35\"/>"
    "<rect x=\"6.7\" y=\"2.1\" width=\"2.6\" height=\"1.5\" rx=\"0.35\" transform=\"rotate(45 8 8)\"/>"
    "<rect x=\"6.7\" y=\"2.1\" width=\"2.6\" height=\"1.5\" rx=\"0.35\" transform=\"rotate(-45 8 8)\"/>"
    "<rect x=\"6.7\" y=\"12.4\" width=\"2.6\" height=\"1.5\" rx=\"0.35\" transform=\"rotate(45 8 8)\"/>"
    "<rect x=\"6.7\" y=\"12.4\" width=\"2.6\" height=\"1.5\" rx=\"0.35\" transform=\"rotate(-45 8 8)\"/>"
    "</g></g>");

inline constexpr const char *SVG_PROPERTIES = ICON_SVG("<path d=\"M2 5h12M2 8h12M2 11h12\"/>"
                                                       "<g fill=\"#ffffff\" stroke=\"none\">"
                                                       "<circle cx=\"6\" cy=\"5\" r=\"1.3\"/>"
                                                       "<circle cx=\"10\" cy=\"8\" r=\"1.3\"/>"
                                                       "<circle cx=\"5\" cy=\"11\" r=\"1.3\"/>"
                                                       "</g>");

inline constexpr const char *SVG_PERSPECTIVE = ICON_SVG("<path d=\"M2 5l4-2 4 2v8l-4 2-4-2V5z\"/>"
                                                        "<path d=\"M10 5l4 2v6l-4 2\"/>");

inline constexpr const char *SVG_VIEWPORT = ICON_SVG("<rect x=\"2\" y=\"3\" width=\"12\" height=\"10\" rx=\"1\"/>"
                                                     "<path d=\"M2 6.3h12\"/>"
                                                     "<path d=\"M3 11l2.2-3.5 1.8 2 2.3-3 2.7 4.5\"/>");

inline constexpr const char *SVG_GRID = ICON_SVG("<path d=\"M2 6h12M2 10h12M6 2v12M10 2v12\"/>");

inline constexpr const char *SVG_LAYERS = ICON_SVG("<path d=\"M8 2l6 3-6 3-6-3 6-3z\"/>"
                                                   "<path d=\"M2 8l6 3 6-3M2 11l6 3 6-3\"/>");

} // namespace Icons

#undef ICON_SVG

#endif // RAPTURE__ICONS_H

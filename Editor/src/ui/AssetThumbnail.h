#ifndef RAPTURE__ASSET_THUMBNAIL_H
#define RAPTURE__ASSET_THUMBNAIL_H

#include <rapture/assets/asset_manager/AssetCommon.h>

#include <amethyst/Amethyst.h>
#include <components/common.h>
#include <components/frame.h>
#include <components/image_label.h>

namespace Rapture {
struct TypeInfo;
} // namespace Rapture

/**
 * @brief An asset's picture on a checkerboard, with a bar in its type's colour under it
 */
class AssetThumbnail {
  public:
    /**
     * @brief Builds the thumbnail under a parent
     * @param parent What the thumbnail is built under
     * @param size Width and height of the picture, a multiple of 8 so the checkerboard ends on whole squares
     */
    AssetThumbnail(Amethyst::UIObject &parent, float size);

    /**
     * @brief Shows an asset that is there
     * @param type The asset's type
     * @param authoredClass The class a module holds, which picks the icon ahead of the type
     * @param picture The asset's rendered picture, or no texture to show its type's icon
     */
    void showAsset(Rapture::AssetType type, const Rapture::TypeInfo *authoredClass,
                   Amethyst::AmTextureId picture = Amethyst::AM_INVALID_TEXTURE);

    /**
     * @brief Shows that nothing is set yet
     * @param expectedType The type the slot takes, or none when it takes more than one
     */
    void showEmpty(Rapture::AssetType expectedType);

    /**
     * @brief Shows that the asset set here no longer exists
     * @param expectedType The type the slot takes, or none when it takes more than one
     */
    void showMissing(Rapture::AssetType expectedType);

    /**
     * @brief Total height of a thumbnail of a given size, its type bar included
     * @param size Width and height of the picture
     * @return The height
     */
    static float heightForSize(float size);

    /**
     * @brief The largest picture whose thumbnail fits a height, kept to a multiple of 8
     * @param height The height there is room for
     * @return Width and height of the picture
     */
    static float sizeForHeight(float height);

    Amethyst::Frame *getRoot() const { return m_root; }

  private:
    /**
     * @brief Shows the checkerboard and puts the base in the class of what is shown
     * @param baseClass The theme class of the base for this state, empty for the plain base
     * @param checkered Whether the checkerboard shows
     */
    void applyBase(std::string_view baseClass, bool checkered);

    /**
     * @brief Shows an svg over the base, scaled to a share of the picture and coloured by its theme class
     * @param svg The svg to show
     * @param share How much of the picture's width it covers
     * @param pictureClass The theme class colouring it, empty for the plain icon colour
     */
    void applyPicture(const char *svg, float share, std::string_view pictureClass);

    /**
     * @brief Colours the bar under the picture by a type, or hides it when there is no one type
     * @param type The type
     */
    void applyTypeBar(Rapture::AssetType type);

  private:
    float m_size = 0.0f;
    Amethyst::Frame *m_root = nullptr;
    Amethyst::Frame *m_base = nullptr;
    Amethyst::Frame *m_squares = nullptr;
    Amethyst::Frame *m_offsetSquares = nullptr;
    Amethyst::ImageLabel *m_picture = nullptr;
    Amethyst::Frame *m_typeBar = nullptr;
};

#endif // RAPTURE__ASSET_THUMBNAIL_H

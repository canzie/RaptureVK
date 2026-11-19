#ifndef RAPTURE__TEXTURE_CANVAS_PANEL_H
#define RAPTURE__TEXTURE_CANVAS_PANEL_H

#include "panels/Panel.h"

#include <rapture/assets/textures/ATexture.h>
#include <rapture/core/utils/Ref.h>
#include <rapture/gpu/textures/TextureView.h>

#include <components/context_menu.h>
#include <components/radio_button.h>

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

class IconLabelButton;

enum TextureCanvasScale {
    TEXTURE_CANVAS_SCALE_FIT,
    TEXTURE_CANVAS_SCALE_ACTUAL_SIZE,
    TEXTURE_CANVAS_SCALE_COUNT
};

/**
 * @brief Shows one texture asset on a canvas, one mip at a time, with its channels shown or hidden
 */
class TextureCanvasPanel : public Panel {
  public:
    static constexpr uint32_t SHOWN_CHANNEL_COUNT = 4;

    std::string_view typeName() const override { return "TextureCanvasPanel"; }

    /**
     * @brief Creates the canvas for a texture asset
     * @param context The workspace the panel is built against
     * @param handle The texture asset shown
     */
    TextureCanvasPanel(const WorkspaceContext &context, Rapture::AssetHandle handle);
    ~TextureCanvasPanel() override;

    void onUpdate(float dt) override;

  protected:
    void build(Amethyst::UIScope &scope) override;
    void onRootDestroyed() override;

  private:
    void buildCheckerboardAndImage(Amethyst::FrameScope &checkerboard);
    void buildMipStrip();
    void buildMipStripEntry(Amethyst::UIScope &strip, uint32_t mip, float aspect);
    void buildTexelReadout();
    void buildToolbar();
    void buildChannelToggles(Amethyst::Frame &toolbar);
    void buildMipMenu(Amethyst::Frame &toolbar);
    void buildScaleToggles(Amethyst::Frame &toolbar);
    void showChannelButtonStates();
    void showScaleButtonStates();
    void showMipStripState();

    /**
     * @brief Shows the texture once its pixels are on the GPU, on the canvas and in the mip strip
     */
    void showTexture();

    /**
     * @brief Points the image at the view for the shown mip and channels, creating that view the first time it is shown
     */
    void showSelectedView();

    /**
     * @brief The UI id of the view onto one mip with the given channels shown, created the first time it is asked for
     * @param mip The mip viewed
     * @param shownChannelBits The channels shown, one bit per channel
     * @return The id the UI draws the view by
     */
    Amethyst::AmTextureId registeredViewId(uint32_t mip, uint8_t shownChannelBits);

    void onImageHoverChanged(bool hovered);
    void onImageCursorMoved(const Amethyst::InputObject &input);

    /**
     * @brief Sizes and centres the image and the checkerboard behind it by the shown mip and the chosen scale
     */
    void layoutImage();

    void setShownMip(uint32_t mip);
    void setChannelShown(Rapture::TextureChannel channel, bool shown);
    void setScale(TextureCanvasScale scale);

  private:
    struct RegisteredTextureView {
        std::unique_ptr<Rapture::TextureView> view;
        Amethyst::AmTextureId id;
    };

    struct MipStripEntry {
        Amethyst::ImageButton *thumbnail = nullptr;
        Amethyst::TextLabel *label = nullptr;
    };

  private:
    Rapture::Ref<Rapture::ATexture> m_texture;
    std::unique_ptr<Rapture::Sampler> m_nearestSampler;

    /// keyed by mip and shown channel bits
    std::unordered_map<uint32_t, RegisteredTextureView> m_registeredViews;

    uint32_t m_shownMip = 0;
    uint8_t m_shownChannelBits = 0;
    TextureCanvasScale m_scale = TEXTURE_CANVAS_SCALE_FIT;
    bool m_textureWasReady = false;
    Amethyst::vec2 m_laidOutCanvasSize{0.0f, 0.0f};

    Amethyst::Frame *m_canvas = nullptr;
    Amethyst::Frame *m_checkerboard = nullptr;
    Amethyst::ImageLabel *m_image = nullptr;
    IconLabelButton *m_mipButton = nullptr;
    Amethyst::ContextMenu *m_mipMenu = nullptr;
    Amethyst::RadioGroup m_mipGroup;
    Amethyst::EventConnection m_mipGroupConn;
    std::array<Amethyst::TextButton *, SHOWN_CHANNEL_COUNT> m_channelButtons{};
    std::array<Amethyst::TextButton *, TEXTURE_CANVAS_SCALE_COUNT> m_scaleButtons{};
    std::vector<MipStripEntry> m_mipStripEntries;
    Amethyst::Frame *m_texelReadoutIsland = nullptr;
    Amethyst::TextLabel *m_texelReadout = nullptr;
};

#endif // RAPTURE__TEXTURE_CANVAS_PANEL_H

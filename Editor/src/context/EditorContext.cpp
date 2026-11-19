#include "EditorContext.h"

#include <rapture/gpu/textures/Texture.h>
#include <rapture/gpu/textures/TextureView.h>

#include <amethyst__vk13_glfw.h>

Amethyst::AmTextureId UiTextures::registerTexture(Rapture::Texture *texture)
{
    if (texture == nullptr) {
        return Amethyst::AM_INVALID_TEXTURE;
    }

    return m_backend->registerTexture(texture->getImageView(), texture->getSampler().getSamplerVk());
}

Amethyst::AmTextureId UiTextures::registerTextureView(const Rapture::TextureView &view, const Rapture::Sampler &sampler)
{
    if (view.getImageView() == VK_NULL_HANDLE) {
        return Amethyst::AM_INVALID_TEXTURE;
    }

    return m_backend->registerTexture(view.getImageView(), sampler.getSamplerVk());
}

void UiTextures::unregisterTexture(Amethyst::AmTextureId id)
{
    if (!id.isValid()) {
        return;
    }

    m_backend->unregisterTexture(id);
}

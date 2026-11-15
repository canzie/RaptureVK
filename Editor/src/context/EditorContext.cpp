#include "EditorContext.h"

#include <rapture/gpu/textures/Texture.h>

#include <amethyst__vk13_glfw.h>

Amethyst::AmTextureId UiTextures::registerTexture(Rapture::Texture *texture)
{
    if (texture == nullptr) {
        return Amethyst::AM_INVALID_TEXTURE;
    }

    return m_backend->registerTexture(texture->getImageView(), texture->getSampler().getSamplerVk());
}

void UiTextures::unregisterTexture(Amethyst::AmTextureId id)
{
    if (!id.isValid()) {
        return;
    }

    m_backend->unregisterTexture(id);
}

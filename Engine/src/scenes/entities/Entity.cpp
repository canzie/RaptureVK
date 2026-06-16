#include "Entity.h"

#include "renderer/SceneRenderData.h"
#include "scenes/Scene.h"

namespace Rapture {

void Entity::markDirty()
{
    if (!isValid()) {
        return;
    }

    SceneRenderData *renderData = m_Scene->getRenderData();
    if (renderData == nullptr) {
        return;
    }

    renderData->markDirty(static_cast<EntityID>(m_EntityHandle));
}

} // namespace Rapture

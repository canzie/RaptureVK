#include "BVH.h"

#include "Scenes/Scene.h"
#include "Components/Components.h"

namespace Rapture {

BVH::BVH(LeafType leafType)
    : m_leafType(leafType), m_nodes({})
{

}

BVH::~BVH() {
    m_nodes.clear();
}

void BVH::build(std::shared_ptr<Scene> scene) {

    auto& reg = scene->getRegistry();

    // one possible limitation of checking the bounding box and not taking into account the collider bb
    // this can however be solved by using a fake bb by including the collider bb and the mesh to calculate the final bb
    // i dont like that but it could work.
    // otherwise we would need to only use colliders, or allow a boundingboxcomponent to act as collider?
    // but only using colliders is also not possible because we need an aabb here...

    // --> so we will do the large aabb including the collider witouth changing the original bounding box component
    // this means we can use the original bounding box component as the argument for the intersection tests.

    auto bbView = reg.view<BoundingBoxComponent>();
    auto colliderView = reg.view<RigidBodyComponent>();

    for (auto entity : bbView) {
        auto& bb = bbView.get<BoundingBoxComponent>(entity);
        auto collider = colliderView.try_get<RigidBodyComponent>(entity);


        if (collider) {

        }




    }

}






}
#ifndef RAPTURE__SCENE_DOCUMENT_HELPERS_H
#define RAPTURE__SCENE_DOCUMENT_HELPERS_H

#include <rapture/core/serialization/SerialDocument.h>
#include <rapture/scene/instances/SceneObject.h>

#include <memory>

/**
 * @brief Writes a scene object and everything under it into a document that can be read
 * @param object The root of the subtree to write
 * @return The document, in read mode
 */
inline std::unique_ptr<Rapture::SerialDocument> SceneDocument_writeSubtree(const Rapture::SceneObject &object)
{
    Rapture::SerialDocument builder;
    object.serialize(builder.root());
    return std::make_unique<Rapture::SerialDocument>(Rapture::SerialDocument::parse(builder.toText()));
}

#endif // RAPTURE__SCENE_DOCUMENT_HELPERS_H

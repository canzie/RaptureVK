#ifndef RAPTURE__SCENE_LOAD_CONTEXT_H
#define RAPTURE__SCENE_LOAD_CONTEXT_H

#include "scene/instances/Instance.h"

#include <unordered_map>
#include <vector>

namespace Rapture {

/**
 * @brief The instances one document read produced, under the ids that document called them by.
 *
 * Lives for the length of the read and no longer, so an id only has to name something while the
 * references written beside it are being turned into pointers.
 */
class SceneLoadContext {
  public:
    /**
     * @brief Opens a read
     * @param remintIds Whether what is read becomes its own objects rather than the ones written
     */
    explicit SceneLoadContext(bool remintIds) : m_remintIds(remintIds) {}

    SceneLoadContext(const SceneLoadContext &) = delete;
    SceneLoadContext &operator=(const SceneLoadContext &) = delete;

    /**
     * @brief Records what a document id was read into
     * @param documentId The id the document gave the instance
     * @param instance The instance read from it
     */
    void addInstance(InstanceId documentId, Instance *instance);

    /**
     * @brief The instance a document id was read into
     * @param documentId The id to look for
     * @return The instance, or nullptr if this read produced none under that id
     */
    Instance *find(InstanceId documentId) const;

    /**
     * @brief Whether what is read takes a fresh identity instead of the document's
     */
    bool remintsIds() const { return m_remintIds; }

    /**
     * @brief Resolves every reference this read produced, then readies what it produced
     */
    void finish();

  private:
    std::unordered_map<InstanceId, Instance *> m_byDocumentId;
    std::vector<Instance *> m_loaded;
    bool m_remintIds;
};

} // namespace Rapture

#endif // RAPTURE__SCENE_LOAD_CONTEXT_H

#include "SceneLoadContext.h"

namespace Rapture {

void SceneLoadContext::addInstance(InstanceId documentId, Instance *instance)
{
    if (instance == nullptr) {
        return;
    }

    if (documentId != INVALID_INSTANCE_ID) {
        m_byDocumentId.emplace(documentId, instance);
    }

    m_loaded.push_back(instance);
}

Instance *SceneLoadContext::find(InstanceId documentId) const
{
    auto found = m_byDocumentId.find(documentId);
    return found != m_byDocumentId.end() ? found->second : nullptr;
}

void SceneLoadContext::finish()
{
    for (Instance *instance : m_loaded) {
        instance->link(*this);
    }

    // children before parents, so an object is readied with everything below it already ready
    for (size_t i = m_loaded.size(); i > 0; i--) {
        m_loaded[i - 1]->ready();
    }
}

} // namespace Rapture

#ifndef RAPTURE__PUPPET_H
#define RAPTURE__PUPPET_H

#include "modules/ModuleClass.h"

namespace Rapture {

class Instance;

/**
 * @brief The scene objects a controller possesses.
 *
 * A puppet is authored outside any scene, so it holds its scene root as a document rather than as
 * scene objects, which need a scene to exist in. Spawning reads that document into a scene, and
 * the scene owns what comes out.
 */
class Puppet : public ModuleClass {
  public:
    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    /**
     * @brief Reads this puppet's scene root into a scene
     * @param parent The scene object the root is parented to, which gains ownership of it
     * @return The spawned root, or nullptr if it could not be read
     */
    Instance *spawn(Instance &parent) const;

    /**
     * @brief Replaces this puppet's scene root with a copy of an authored one
     * @param root The scene object whose subtree becomes this puppet's
     */
    void capture(const Instance &root);

    bool hasSceneRoot() const { return m_sceneRoot.isReadable(); }

    void serialize(WriteNode node) const override;
    void deserialize(ReadNode node) override;

  private:
    SerialDocument m_sceneRoot;
};

} // namespace Rapture

#endif // RAPTURE__PUPPET_H

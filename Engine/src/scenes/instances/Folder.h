#ifndef RAPTURE__FOLDER_H
#define RAPTURE__FOLDER_H

#include "scenes/instances/SceneObject.h"

namespace Rapture {

/**
 * @brief Groups instances in the outliner without taking part in transforms.
 */
class Folder : public SceneObject {
  public:
    Folder(Scene &scene, std::string_view name);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;
};

} // namespace Rapture

#endif // RAPTURE__FOLDER_H

#include "Controller.h"

namespace Rapture {

const TypeInfo &Controller::staticType()
{
    static const TypeInfo type("Controller", &ModuleClass::staticType());
    return type;
}

const TypeInfo &Controller::type() const
{
    return staticType();
}

void Controller::possess(SceneObject *subject)
{
    m_possessed = subject;
}

void Controller::unpossess()
{
    m_possessed = nullptr;
}

} // namespace Rapture

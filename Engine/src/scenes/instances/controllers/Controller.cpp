#include "Controller.h"

namespace Rapture {

Controller::Controller(Scene &scene, std::string_view name) : SceneObject(scene, name)
{
    setTickPhase(TICK_INPUT);
    setTickEnabled(true);
}

const TypeInfo &Controller::staticType()
{
    static const TypeInfo type("Controller", &SceneObject::staticType());
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

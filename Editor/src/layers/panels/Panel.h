#ifndef RAPTURE__PANEL_H
#define RAPTURE__PANEL_H

#include <amethyst/Amethyst.h>

class Panel {
  public:
    virtual ~Panel() = default;
    virtual void onUpdate(float dt) {}

  protected:
    Amethyst::EventConnection m_rootDestroyConn;
};

#endif // RAPTURE__PANEL_H

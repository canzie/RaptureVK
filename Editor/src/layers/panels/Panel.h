#ifndef RAPTURE__PANEL_H
#define RAPTURE__PANEL_H

class Panel {
  public:
    virtual ~Panel() = default;
    virtual void onUpdate(float dt) {}
};

#endif // RAPTURE__PANEL_H

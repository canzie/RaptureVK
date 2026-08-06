#ifndef RAPTURE__TEST_LAYER_H
#define RAPTURE__TEST_LAYER_H

#include "layers/Layer.h"

#include <cstdint>

/**
 * @brief Reports the frame rate and the hardware readings once a second.
 */
class TestLayer : public Rapture::Layer {
  public:
    TestLayer();

    void onUpdate(float ts) override;

  protected:
    void onAttach() override;
    void onDetach() override;

  private:
    uint32_t m_frameCount = 0;
    float m_elapsed = 0.0f;
};

#endif // RAPTURE__TEST_LAYER_H

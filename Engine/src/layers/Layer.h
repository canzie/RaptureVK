#ifndef RAPTURE__LAYER_H
#define RAPTURE__LAYER_H

#include <cstdint>
#include <string>
#include <string_view>

namespace Rapture {

/**
 * @brief One participant in the frame, updated for as long as it is attached.
 */
class Layer {
  public:
    /**
     * @brief Creates a layer
     * @param name Name for logs and the profiler, defaulted to a numbered one when left empty
     */
    explicit Layer(std::string_view name = {});

    virtual ~Layer() = default;

    Layer(const Layer &) = delete;
    Layer &operator=(const Layer &) = delete;

    virtual void onUpdate(float ts) = 0;

    /**
     * @brief Brings this layer into the frame, doing nothing if it is already attached
     */
    void attach();

    /**
     * @brief Stands this layer down, doing nothing if it is already detached
     */
    void detach();

    bool isAttached() const { return m_attached; }
    std::string_view name() const { return m_name; }

  protected:
    virtual void onAttach() = 0;
    virtual void onDetach() = 0;

  private:
    static uint32_t m_nextLayerId;

    std::string m_name;
    bool m_attached = false;
};

} // namespace Rapture

#endif // RAPTURE__LAYER_H

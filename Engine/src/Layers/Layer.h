#pragma once
#include "Events/Events.h"
#include "Scenes/Scene.h"

#include <string>

namespace Rapture {
	static unsigned int s_layer_ID = 0;
    
    //using pushLayerCallback = std::function<void(Layer*)>;

	class Layer {
	public:
		Layer(const std::string name = "Layer_"+std::to_string(s_layer_ID))
			: m_debug_name(name) {
			s_layer_ID++;
		}
        
        virtual ~Layer() = 0;

		virtual void onDetach() = 0;
		virtual void onAttach() = 0;
		virtual void onUpdate(float ts) = 0;

        //void setPushLayerCallback(const pushLayerCallback& callback) { m_pushLayerCallback = callback; }

		std::string getLayerName() { return m_debug_name; }
		// Alias to match the function name used in profiler
		const char* getName() { return m_debug_name.c_str(); }
		
	private:
        //pushLayerCallback m_pushLayerCallback;
		std::string m_debug_name;

	};

    // Empty implementation for pure virtual destructor
    inline Layer::~Layer() { }
}
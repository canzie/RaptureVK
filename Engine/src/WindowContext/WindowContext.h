#pragma once

//#include "../Events/Events.h"
#include "../Events/ApplicationEvents.h"

#include <functional>

namespace Rapture {

	// Buffer swap mode enumeration
	enum class SwapMode {
		Immediate,        // No VSync, uncapped framerate (double buffering)
		VSync,            // Traditional VSync with double buffering
		AdaptiveVSync,    // Adaptive VSync with triple buffering (if supported)
		TripleBuffering   // Triple buffering without VSync (uncapped framerate)
	};

	class WindowContext {

	public:
		virtual ~WindowContext() {};

		// create context and set the callbacks
		virtual void initWindow(void) = 0;
		virtual void closeWindow(void) = 0;

		virtual void onUpdate(void) = 0;

		virtual void* getNativeWindowContext() = 0;

		virtual void getFramebufferSize(int* width, int* height) const = 0;

        virtual const char** getExtensions() = 0;
        virtual uint32_t getExtensionCount() = 0;
		
		// Buffer swap control functions (implemented by derived classes)
		virtual void setSwapMode(SwapMode mode) {}
		virtual SwapMode getSwapMode() const { return static_cast<SwapMode>(0); } // Default implementation
		virtual bool isTripleBufferingSupported() const { return false; }

		static WindowContext* createWindow(int width, int height, const char* title);


	protected:

		struct ContextData {
			int height;
			int width;
		} m_context_data;


	};


}
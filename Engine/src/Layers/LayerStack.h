#include "Layer.h"
#include <vector>



namespace Rapture {

	class LayerStack {

	public:
		void pushLayer(Layer* layer);
		void pushOverlay(Layer* overlay);
		void popLayer(Layer* layer);
		void popOverlay(Layer* overlay);

		std::vector<Layer*>::iterator begin() { return m_Layers.begin(); }
		std::vector<Layer*>::iterator end() { return m_Layers.end(); }
		std::vector<Layer*>::reverse_iterator rbegin() { return m_Layers.rbegin(); }
		std::vector<Layer*>::reverse_iterator rend() { return m_Layers.rend(); }

		std::vector<Layer*>::const_iterator begin() const { return m_Layers.begin(); }
		std::vector<Layer*>::const_iterator end()	const { return m_Layers.end(); }
		std::vector<Layer*>::const_reverse_iterator rbegin() const { return m_Layers.rbegin(); }
		std::vector<Layer*>::const_reverse_iterator rend() const { return m_Layers.rend(); }

        void clear();

        // Iterators over regular layers (those pushed before the first overlay)
        std::vector<Layer*>::iterator layerBegin() { return m_Layers.begin(); }
        std::vector<Layer*>::iterator layerEnd() { return m_Layers.begin() + m_LayerInsertIndex; }
        std::vector<Layer*>::const_iterator layerBegin() const { return m_Layers.begin(); }
        std::vector<Layer*>::const_iterator layerEnd() const { return m_Layers.begin() + m_LayerInsertIndex; }

        // Iterators over overlay layers (those pushed after regular layers)
        std::vector<Layer*>::iterator overlayBegin() { return m_Layers.begin() + m_LayerInsertIndex; }
        std::vector<Layer*>::iterator overlayEnd() { return m_Layers.end(); }
        std::vector<Layer*>::const_iterator overlayBegin() const { return m_Layers.begin() + m_LayerInsertIndex; }
        std::vector<Layer*>::const_iterator overlayEnd() const { return m_Layers.end(); }

	private:
		std::vector<Layer*> m_Layers;
		unsigned int m_LayerInsertIndex = 0;
	};

}
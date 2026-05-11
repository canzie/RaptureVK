#include "LayerStack.h"

void Rapture::LayerStack::pushLayer(Layer* layer)
{
	m_Layers.emplace(m_Layers.begin() + m_LayerInsertIndex, layer);
	m_LayerInsertIndex++;
}

void Rapture::LayerStack::pushOverlay(Layer* overlay)
{
	// Overlays are pushed at the end of the vector, after all regular layers
	m_Layers.emplace_back(overlay);
}

void Rapture::LayerStack::popLayer(Layer* layer)
{
	// Find the layer
	auto it = std::find(m_Layers.begin(), m_Layers.begin() + m_LayerInsertIndex, layer);
	if (it != m_Layers.begin() + m_LayerInsertIndex)
	{
		layer->onDetach();
		m_Layers.erase(it);
		m_LayerInsertIndex--;
	}
}

void Rapture::LayerStack::popOverlay(Layer* overlay)
{
	// Find the overlay (which is after the regular layers)
	auto it = std::find(m_Layers.begin() + m_LayerInsertIndex, m_Layers.end(), overlay);
	if (it != m_Layers.end())
	{
		overlay->onDetach();
		m_Layers.erase(it);
	}
}

void Rapture::LayerStack::clear()
{
	for (auto layer : m_Layers)
	{
		layer->onDetach();
		delete layer;
	}
	m_Layers.clear();
	m_LayerInsertIndex = 0;
}

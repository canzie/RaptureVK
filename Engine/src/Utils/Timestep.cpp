#include "Timestep.h"
#include <cmath>

namespace Rapture
{
	std::chrono::seconds Timestep::getSeconds()
	{
		return std::chrono::duration_cast<std::chrono::seconds>(m_time);
	}

	std::chrono::milliseconds Timestep::getMilliSeconds()
	{
		return m_time;
	}

	std::chrono::milliseconds Timestep::deltaTimeMs()
	{
		return m_time-m_lastFrameTime;
	}
	
	float Timestep::deltaTime()
	{
		// Convert milliseconds to seconds as a float
		return deltaTimeMs().count() / 1000.0f;
	}

	std::chrono::milliseconds Timestep::getTimeSinceLaunchMs()
	{
		// Return the cached value calculated in onUpdate()
		return m_timeSinceLaunch;
	}
	

	

	void Timestep::onUpdate()
	{
		m_lastFrameTime = m_time;
		m_time = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch());

		// Calculate and cache time since launch using the updated m_time
		m_timeSinceLaunch = m_time - std::chrono::duration_cast<std::chrono::milliseconds>(m_launchTime.time_since_epoch());
	}

	std::chrono::milliseconds Timestep::m_time = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch());

	std::chrono::milliseconds Timestep::m_lastFrameTime = m_time;

	// Initialize launch time when the class is loaded
	std::chrono::time_point<std::chrono::system_clock> Timestep::m_launchTime = std::chrono::system_clock::now();

	// Initialize cached time since launch
	std::chrono::milliseconds Timestep::m_timeSinceLaunch = std::chrono::milliseconds(0);

}

/**




			{

		};
	*/
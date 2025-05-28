#include <ctime>
#include <chrono>


namespace Rapture
{

	class Timestep
	{

	public:

		static std::chrono::seconds getSeconds();
		static std::chrono::milliseconds getMilliSeconds();
		static std::chrono::milliseconds deltaTimeMs();
		static float deltaTime();
		static std::chrono::milliseconds getTimeSinceLaunchMs();

		static void onUpdate();

	private:
		static std::chrono::milliseconds m_time;
		static std::chrono::milliseconds m_lastFrameTime;
		static std::chrono::milliseconds m_timeSinceLaunch;
		static std::chrono::time_point<std::chrono::system_clock> m_launchTime;
	};

}
	

#pragma once

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


    class Stopwatch {
    public:
        Stopwatch() = default;
        ~Stopwatch() = default;
        void start();
        void stop();

        uint64_t getElapsedTimeMs() const { return m_elapsedTime.count(); }

    private:
        std::chrono::time_point<std::chrono::system_clock> m_startTime;
        std::chrono::time_point<std::chrono::system_clock> m_endTime;
        bool m_isRunning;
        std::chrono::milliseconds m_elapsedTime;
    };


}
	

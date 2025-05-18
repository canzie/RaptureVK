#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>
#include <string>
#include <vector>
#include <memory>

namespace Rapture {

	enum class LogCategory {
		Core,
		Client,
		Debug,
		Render,
		Physics,
		Audio
	};

	struct LogMessage {
		std::string message;
		spdlog::level::level_enum level;
		LogCategory category;
		std::string timestamp;
	};

	class Log
	{
	public:
		static void Init();
		static void Shutdown();

		// Get specific loggers
		static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
		static std::shared_ptr<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }
		static std::shared_ptr<spdlog::logger>& GetRenderLogger() { return s_RenderLogger; }
		static std::shared_ptr<spdlog::logger>& GetPhysicsLogger() { return s_PhysicsLogger; }
		static std::shared_ptr<spdlog::logger>& GetAudioLogger() { return s_AudioLogger; }

		// Log storage methods
		static const std::vector<LogMessage>& GetRecentLogs() { return s_RecentLogs; }
		static void ClearRecentLogs() { s_RecentLogs.clear(); }
		static void SetMaxRecentLogs(size_t count) { s_MaxRecentLogs = count; }
		
		// Set global log level
		static void SetLogLevel(spdlog::level::level_enum level);

		// Log to specific file
		static void EnableFileLogging(const std::string& filename, LogCategory categories);
		static void DisableFileLogging(const std::string& filename);

	private:
		static std::shared_ptr<spdlog::logger> s_CoreLogger;
		static std::shared_ptr<spdlog::logger> s_ClientLogger;
		static std::shared_ptr<spdlog::logger> s_RenderLogger;
		static std::shared_ptr<spdlog::logger> s_PhysicsLogger;
		static std::shared_ptr<spdlog::logger> s_AudioLogger;

		static std::vector<LogMessage> s_RecentLogs;
		static size_t s_MaxRecentLogs;
		
		// Internal callback to store logs for display
		static void LogCallback(const spdlog::details::log_msg& msg, LogCategory category);
	};

}

// Core log macros
#define RP_CORE_TRACE(...)    Log::GetCoreLogger()->trace(__VA_ARGS__)
#define RP_CORE_INFO(...)     Log::GetCoreLogger()->info(__VA_ARGS__)
#define RP_CORE_WARN(...)     Log::GetCoreLogger()->warn(__VA_ARGS__)
#define RP_CORE_ERROR(...)    Log::GetCoreLogger()->error(__VA_ARGS__)
#define RP_CORE_CRITICAL(...) Log::GetCoreLogger()->critical(__VA_ARGS__)

// Client log macros
#define RP_TRACE(...)         Log::GetClientLogger()->trace(__VA_ARGS__)
#define RP_INFO(...)          Log::GetClientLogger()->info(__VA_ARGS__)
#define RP_WARN(...)          Log::GetClientLogger()->warn(__VA_ARGS__)
#define RP_ERROR(...)         Log::GetClientLogger()->error(__VA_ARGS__)
#define RP_CRITICAL(...)      Log::GetClientLogger()->critical(__VA_ARGS__)


// Render log macros
#define RP_RENDER_TRACE(...)  Log::GetRenderLogger()->trace(__VA_ARGS__)
#define RP_RENDER_INFO(...)   Log::GetRenderLogger()->info(__VA_ARGS__)
#define RP_RENDER_WARN(...)   Log::GetRenderLogger()->warn(__VA_ARGS__)
#define RP_RENDER_ERROR(...)  Log::GetRenderLogger()->error(__VA_ARGS__)
#define RP_RENDER_FATAL(...)  Log::GetRenderLogger()->critical(__VA_ARGS__)

// Physics log macros
#define RP_PHYSICS_TRACE(...) Log::GetPhysicsLogger()->trace(__VA_ARGS__)
#define RP_PHYSICS_INFO(...)  Log::GetPhysicsLogger()->info(__VA_ARGS__)
#define RP_PHYSICS_WARN(...)  Log::GetPhysicsLogger()->warn(__VA_ARGS__)
#define RP_PHYSICS_ERROR(...) Log::GetPhysicsLogger()->error(__VA_ARGS__)
#define RP_PHYSICS_FATAL(...) Log::GetPhysicsLogger()->critical(__VA_ARGS__)

// Audio log macros
#define RP_AUDIO_TRACE(...)   Log::GetAudioLogger()->trace(__VA_ARGS__)
#define RP_AUDIO_INFO(...)    Log::GetAudioLogger()->info(__VA_ARGS__)
#define RP_AUDIO_WARN(...)    Log::GetAudioLogger()->warn(__VA_ARGS__)
#define RP_AUDIO_ERROR(...)   Log::GetAudioLogger()->error(__VA_ARGS__)
#define RP_AUDIO_FATAL(...)   Log::GetAudioLogger()->critical(__VA_ARGS__)
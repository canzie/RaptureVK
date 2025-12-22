#ifndef RAPTURE__LOG_H
#define RAPTURE__LOG_H

#include <memory>
#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/ostr.h>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

#if defined(_MSC_VER)
    #define FUNCTION_SIGNATURE __FUNCSIG__
#elif defined(__GNUC__) || defined(__clang__)
    #define FUNCTION_SIGNATURE __PRETTY_FUNCTION__
#else
    #define FUNCTION_SIGNATURE __func__
#endif

namespace Rapture {

// Helper function to extract Class::Method from __PRETTY_FUNCTION__
// Assumes format: ReturnType Namespace::Class::Method(Args)
inline std::string s_extractFunctionInfo(const char *prettyFunction)
{
    std::string func(prettyFunction);

    // For GCC/Clang: Format is typically "ReturnType Namespace::Class::Method(Args) [with ...]"
    // Extract the part before the opening parenthesis
    size_t parenPos = func.find('(');
    if (parenPos != std::string::npos) {
        func = func.substr(0, parenPos);
    }

    // Remove template parameters if present
    size_t bracketPos = func.find('[');
    if (bracketPos != std::string::npos) {
        func = func.substr(0, bracketPos);
    }

    // Find the first :: (assumed to be after namespace)
    size_t firstScope = func.find("::");
    if (firstScope != std::string::npos && firstScope + 2 < func.length()) {
        // Skip namespace (first component), return Class::Method (everything after first ::)
        return func.substr(firstScope + 2);
    }

    // Fallback: return the function name after the last space
    size_t lastSpace = func.find_last_of(" ");
    if (lastSpace != std::string::npos && lastSpace + 1 < func.length()) {
        return func.substr(lastSpace + 1);
    }

    return func;
}

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

class Log {
  public:
    static void Init();
    static void Shutdown();

    // Get specific loggers
    static std::shared_ptr<spdlog::logger> &GetCoreLogger() { return s_CoreLogger; }
    static std::shared_ptr<spdlog::logger> &GetClientLogger() { return s_ClientLogger; }
    static std::shared_ptr<spdlog::logger> &GetRenderLogger() { return s_RenderLogger; }
    static std::shared_ptr<spdlog::logger> &GetPhysicsLogger() { return s_PhysicsLogger; }
    static std::shared_ptr<spdlog::logger> &GetAudioLogger() { return s_AudioLogger; }

    // Log storage methods
    static const std::vector<LogMessage> &GetRecentLogs() { return s_RecentLogs; }
    static void ClearRecentLogs() { s_RecentLogs.clear(); }
    static void SetMaxRecentLogs(size_t count) { s_MaxRecentLogs = count; }

    // Set global log level
    static void SetLogLevel(spdlog::level::level_enum level);

    // Log to specific file
    static void EnableFileLogging(const std::string &filename, LogCategory categories);
    static void DisableFileLogging(const std::string &filename);

  private:
    static std::shared_ptr<spdlog::logger> s_CoreLogger;
    static std::shared_ptr<spdlog::logger> s_ClientLogger;
    static std::shared_ptr<spdlog::logger> s_RenderLogger;
    static std::shared_ptr<spdlog::logger> s_PhysicsLogger;
    static std::shared_ptr<spdlog::logger> s_AudioLogger;

    static std::vector<LogMessage> s_RecentLogs;
    static size_t s_MaxRecentLogs;

    // Internal callback to store logs for display
    static void LogCallback(const spdlog::details::log_msg &msg, LogCategory category);
};

} // namespace Rapture

// Helper macros to format log message with class/method and severity
#define RP_LOG_TRACE(logger, ...) \
    logger->trace("[TRACE] {}: {}", Rapture::s_extractFunctionInfo(FUNCTION_SIGNATURE), fmt::format(__VA_ARGS__))
#define RP_LOG_INFO(logger, ...) \
    logger->info("[INFO] {}: {}", Rapture::s_extractFunctionInfo(FUNCTION_SIGNATURE), fmt::format(__VA_ARGS__))
#define RP_LOG_WARN(logger, ...) \
    logger->warn("[WARN] {}: {}", Rapture::s_extractFunctionInfo(FUNCTION_SIGNATURE), fmt::format(__VA_ARGS__))
#define RP_LOG_ERROR(logger, ...) \
    logger->error("[ERROR] {}: {}", Rapture::s_extractFunctionInfo(FUNCTION_SIGNATURE), fmt::format(__VA_ARGS__))
#define RP_LOG_CRITICAL(logger, ...) \
    logger->critical("[CRITICAL] {}: {}", Rapture::s_extractFunctionInfo(FUNCTION_SIGNATURE), fmt::format(__VA_ARGS__))

// Core log macros
#define RP_CORE_TRACE(...)    RP_LOG_TRACE(Log::GetCoreLogger(), __VA_ARGS__)
#define RP_CORE_INFO(...)     RP_LOG_INFO(Log::GetCoreLogger(), __VA_ARGS__)
#define RP_CORE_WARN(...)     RP_LOG_WARN(Log::GetCoreLogger(), __VA_ARGS__)
#define RP_CORE_ERROR(...)    RP_LOG_ERROR(Log::GetCoreLogger(), __VA_ARGS__)
#define RP_CORE_CRITICAL(...) RP_LOG_CRITICAL(Log::GetCoreLogger(), __VA_ARGS__)

// Client log macros
#define RP_TRACE(...)    RP_LOG_TRACE(Log::GetClientLogger(), __VA_ARGS__)
#define RP_INFO(...)     RP_LOG_INFO(Log::GetClientLogger(), __VA_ARGS__)
#define RP_WARN(...)     RP_LOG_WARN(Log::GetClientLogger(), __VA_ARGS__)
#define RP_ERROR(...)    RP_LOG_ERROR(Log::GetClientLogger(), __VA_ARGS__)
#define RP_CRITICAL(...) RP_LOG_CRITICAL(Log::GetClientLogger(), __VA_ARGS__)

// Render log macros
#define RP_RENDER_TRACE(...) RP_LOG_TRACE(Log::GetRenderLogger(), __VA_ARGS__)
#define RP_RENDER_INFO(...)  RP_LOG_INFO(Log::GetRenderLogger(), __VA_ARGS__)
#define RP_RENDER_WARN(...)  RP_LOG_WARN(Log::GetRenderLogger(), __VA_ARGS__)
#define RP_RENDER_ERROR(...) RP_LOG_ERROR(Log::GetRenderLogger(), __VA_ARGS__)
#define RP_RENDER_FATAL(...) RP_LOG_CRITICAL(Log::GetRenderLogger(), __VA_ARGS__)

// Physics log macros
#define RP_PHYSICS_TRACE(...) RP_LOG_TRACE(Log::GetPhysicsLogger(), __VA_ARGS__)
#define RP_PHYSICS_INFO(...)  RP_LOG_INFO(Log::GetPhysicsLogger(), __VA_ARGS__)
#define RP_PHYSICS_WARN(...)  RP_LOG_WARN(Log::GetPhysicsLogger(), __VA_ARGS__)
#define RP_PHYSICS_ERROR(...) RP_LOG_ERROR(Log::GetPhysicsLogger(), __VA_ARGS__)
#define RP_PHYSICS_FATAL(...) RP_LOG_CRITICAL(Log::GetPhysicsLogger(), __VA_ARGS__)

// Audio log macros
#define RP_AUDIO_TRACE(...) RP_LOG_TRACE(Log::GetAudioLogger(), __VA_ARGS__)
#define RP_AUDIO_INFO(...)  RP_LOG_INFO(Log::GetAudioLogger(), __VA_ARGS__)
#define RP_AUDIO_WARN(...)  RP_LOG_WARN(Log::GetAudioLogger(), __VA_ARGS__)
#define RP_AUDIO_ERROR(...) RP_LOG_ERROR(Log::GetAudioLogger(), __VA_ARGS__)
#define RP_AUDIO_FATAL(...) RP_LOG_CRITICAL(Log::GetAudioLogger(), __VA_ARGS__)

#endif // RAPTURE__LOG_H
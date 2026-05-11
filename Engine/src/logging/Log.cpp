#include "Log.h"

#include <map>
#include <mutex>
#include <set>
#include <spdlog/details/log_msg.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/callback_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Rapture {

std::shared_ptr<spdlog::logger> Log::s_CoreLogger;
std::shared_ptr<spdlog::logger> Log::s_ClientLogger;
std::shared_ptr<spdlog::logger> Log::s_RenderLogger;
std::shared_ptr<spdlog::logger> Log::s_PhysicsLogger;
std::shared_ptr<spdlog::logger> Log::s_AudioLogger;

std::vector<LogMessage> Log::s_RecentLogs;
size_t Log::s_MaxRecentLogs = 1000;

// Store file sinks by filename
static std::map<std::string, std::shared_ptr<spdlog::sinks::rotating_file_sink_mt>> s_RotatingFileSinks;
static std::map<std::string, std::shared_ptr<spdlog::sinks::basic_file_sink_mt>> s_BasicFileSinks;
// Map to track which categories are logged to which files
static std::map<std::string, std::set<LogCategory>> s_FileSinkCategories;
static std::mutex s_LogMutex;

void Log::Init()
{
    // Configure common sinks
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_pattern("%^[%T] %n: %v%$");

    // Rotating file sink with 5MB size limit, 3 rotated files
    auto defaultFileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>("logs/Rapture.log", 5 * 1024 * 1024, 3);
    defaultFileSink->set_pattern("[%Y-%m-%d %T.%e] [%l] %n: %v");

    // Store the default file sink
    s_RotatingFileSinks["logs/Rapture.log"] = defaultFileSink;

    // Create a callback sink for logging to memory
    auto callbackSink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg &msg) {
        LogCategory category = LogCategory::Core; // Default
        Log::LogCallback(msg, category);
    });
    callbackSink->set_pattern("[%T.%e] [%^%l%$] %n: %v");

    // Helper to create loggers with appropriate sinks
    auto createLogger = [&](const std::string &name, LogCategory category) {
        std::vector<spdlog::sink_ptr> sinks;
        sinks.push_back(consoleSink);
        sinks.push_back(defaultFileSink);

        // Create specialized callback for this category
        auto categorizedCallbackSink = std::make_shared<spdlog::sinks::callback_sink_mt>(
            [category](const spdlog::details::log_msg &msg) { Log::LogCallback(msg, category); });
        categorizedCallbackSink->set_pattern("[%T.%e] [%^%l%$] %n: %v");
        sinks.push_back(categorizedCallbackSink);

        auto logger = std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());
        logger->set_level(spdlog::level::trace);
        logger->flush_on(spdlog::level::info);
        spdlog::register_logger(logger);
        return logger;
    };

    // Initialize all loggers
    s_CoreLogger = createLogger("RAPTURE", LogCategory::Core);
    s_ClientLogger = createLogger("EDITOR", LogCategory::Client);
    s_RenderLogger = createLogger("RENDER", LogCategory::Render);
    s_PhysicsLogger = createLogger("PHYSICS", LogCategory::Physics);
    s_AudioLogger = createLogger("AUDIO", LogCategory::Audio);

    // Set custom colors for each logger
    spdlog::set_pattern("%^[%T] %n: %v%$");

#ifdef NDEBUG
    SetLogLevel(spdlog::level::info);
#else
    SetLogLevel(spdlog::level::trace);
#endif

    // Set up default file sink categories
    std::set<LogCategory> allCategories = {LogCategory::Core, LogCategory::Client, LogCategory::Render, LogCategory::Physics,
                                           LogCategory::Audio};
    s_FileSinkCategories["logs/Rapture.log"] = allCategories;

    // Create render-specific log file
    EnableFileLogging("logs/render.log", LogCategory::Render);

    // Create physics-specific log file
    EnableFileLogging("logs/physics.log", LogCategory::Physics);

    s_CoreLogger->info("Logger initialized with advanced features");
}

void Log::Shutdown()
{
    s_CoreLogger->info("Shutting down logger");
    s_CoreLogger.reset();
    s_ClientLogger.reset();
    s_RenderLogger.reset();
    s_PhysicsLogger.reset();
    s_AudioLogger.reset();

    s_RotatingFileSinks.clear();
    s_BasicFileSinks.clear();
    s_FileSinkCategories.clear();
    s_RecentLogs.clear();

    spdlog::shutdown();
}

void Log::SetLogLevel(spdlog::level::level_enum level)
{
    s_CoreLogger->set_level(level);
    s_ClientLogger->set_level(level);
    s_RenderLogger->set_level(level);
    s_PhysicsLogger->set_level(level);
    s_AudioLogger->set_level(level);
}

void Log::EnableFileLogging(const std::string &filename, LogCategory category)
{
    std::shared_ptr<spdlog::logger> targetLogger = nullptr;
    bool addedSink = false; // Flag to track if a sink was actually added

    {
        std::lock_guard<std::mutex> lock(s_LogMutex);

        // Create sink if it doesn't exist
        if (s_BasicFileSinks.find(filename) == s_BasicFileSinks.end() &&
            s_RotatingFileSinks.find(filename) == s_RotatingFileSinks.end()) {
            s_BasicFileSinks[filename] = std::make_shared<spdlog::sinks::basic_file_sink_mt>(filename, true);
            s_BasicFileSinks[filename]->set_pattern("[%Y-%m-%d %T.%e] [%l] %n: %v");
            s_FileSinkCategories[filename] = std::set<LogCategory>();
            addedSink = true; // Mark that a new sink was created
        }

        // Add category to file's categories
        if (s_FileSinkCategories.count(filename)) {
            s_FileSinkCategories[filename].insert(category);
        }

        // Determine the target logger based on category
        switch (category) {
        case LogCategory::Debug:
        case LogCategory::Core:
            targetLogger = s_CoreLogger;
            break;
        case LogCategory::Client:
            targetLogger = s_ClientLogger;
            break;
        case LogCategory::Render:
            targetLogger = s_RenderLogger;
            break;
        case LogCategory::Physics:
            targetLogger = s_PhysicsLogger;
            break;
        case LogCategory::Audio:
            targetLogger = s_AudioLogger;
            break;
        }

        // Add file sink to appropriate logger if the logger exists
        if (targetLogger) {
            spdlog::sink_ptr sinkToAdd = nullptr;
            if (s_BasicFileSinks.count(filename)) {
                sinkToAdd = s_BasicFileSinks[filename];
            } else if (s_RotatingFileSinks.count(filename)) {
                sinkToAdd = s_RotatingFileSinks[filename];
            }

            if (sinkToAdd) {
                // Check if the sink is already present to avoid duplicates
                auto &sinks = targetLogger->sinks();
                bool alreadyExists = false;
                for (const auto &existingSink : sinks) {
                    if (existingSink == sinkToAdd) {
                        alreadyExists = true;
                        break;
                    }
                }
                if (!alreadyExists) {
                    sinks.push_back(sinkToAdd);
                    addedSink = true; // Mark that a sink was added to this logger
                }
            }
        }
    } // Mutex lock_guard goes out of scope here

    // Log outside the mutex lock
    if (targetLogger && addedSink) {
        // Use targetLogger->name() directly if it's guaranteed to be valid
        std::string loggerName = targetLogger ? targetLogger->name() : "UnknownLogger";
        s_CoreLogger->info("Enabled logging for {} to file {}", loggerName, filename);
    }
}

void Log::DisableFileLogging(const std::string &filename)
{
    std::lock_guard<std::mutex> lock(s_LogMutex);

    // Get appropriate sink
    spdlog::sink_ptr fileSink = nullptr;
    if (s_BasicFileSinks.find(filename) != s_BasicFileSinks.end()) {
        fileSink = s_BasicFileSinks[filename];
    } else if (s_RotatingFileSinks.find(filename) != s_RotatingFileSinks.end()) {
        fileSink = s_RotatingFileSinks[filename];
    } else {
        return;
    }

    // Remove sink from all loggers
    auto removeFromLogger = [&fileSink](std::shared_ptr<spdlog::logger> &logger) {
        if (!logger) return;

        auto &sinks = logger->sinks();
        sinks.erase(std::remove(sinks.begin(), sinks.end(), fileSink), sinks.end());
    };

    removeFromLogger(s_CoreLogger);
    removeFromLogger(s_ClientLogger);
    removeFromLogger(s_RenderLogger);
    removeFromLogger(s_PhysicsLogger);
    removeFromLogger(s_AudioLogger);

    // Remove sink from tracking
    if (s_BasicFileSinks.find(filename) != s_BasicFileSinks.end()) {
        s_BasicFileSinks.erase(filename);
    } else if (s_RotatingFileSinks.find(filename) != s_RotatingFileSinks.end()) {
        s_RotatingFileSinks.erase(filename);
    }

    s_FileSinkCategories.erase(filename);

    s_CoreLogger->info("Disabled logging to file {}", filename);
}

void Log::LogCallback(const spdlog::details::log_msg &msg, LogCategory category)
{
    std::lock_guard<std::mutex> lock(s_LogMutex);

    // Get formatted message
    spdlog::memory_buf_t formatted;
    spdlog::pattern_formatter formatter("[%T.%e] [%^%l%$] %n: %v");
    formatter.format(msg, formatted);

    // Add message to recent logs
    LogMessage logMsg;
    logMsg.message = fmt::to_string(formatted);
    logMsg.level = msg.level;
    logMsg.category = category;

    // Add timestamp
    char timestamp[64];
    time_t rawtime;
    struct tm timeinfo;
    time(&rawtime);
#ifdef _WIN32
    localtime_s(&timeinfo, &rawtime);
#elif __linux__
    localtime_r(&rawtime, &timeinfo);
#endif
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);
    logMsg.timestamp = timestamp;

    // Add to recent logs
    s_RecentLogs.push_back(logMsg);

    // Trim if too large
    if (s_RecentLogs.size() > s_MaxRecentLogs) {
        s_RecentLogs.erase(s_RecentLogs.begin());
    }
}

} // namespace Rapture
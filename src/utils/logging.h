#ifndef SCANNER_LOGGING_H
#define SCANNER_LOGGING_H

#include <filesystem>
#include <fmt/format.h>
#include <glog/logging.h>
#include <probe/thread.h>

class Logger
{
public:
    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&)                 = delete;
    Logger& operator=(Logger&&)      = delete;

    static Logger& init(const char *argv0)
    {
        static Logger logger(argv0);
        return logger;
    }

    ~Logger() { google::ShutdownGoogleLogging(); }

private:
    explicit Logger(const char *argv0)
    {
#ifdef _WIN32
        const std::string log_dir = "logs";
#elif __linux__
        const std::string log_dir = std::string{ ::getenv("HOME") } + "/.local/logs/scanner";
#endif
        if (!std::filesystem::is_directory(log_dir) || std::filesystem::exists(log_dir)) {
            std::filesystem::create_directories(log_dir);
        }

        google::InitGoogleLogging(argv0);

        // clang-format off
        google::InstallPrefixFormatter(
            [](std::ostream& s, const google::LogMessage& m, void*)
            {
                std::string file_line = m.basename() + std::string(":") + std::to_string(m.line());
                file_line = file_line.substr(std::max<int64_t >(0, static_cast<int64_t>(file_line.size()) - 24), 24);

                s
                    << std::setw(4) << 1900 + m.time().year() << '-'
                    << std::setw(2) << 1 + m.time().month() << '-'
                    << std::setw(2) << m.time().day()
                    << ' '
                    << std::setw(2) << m.time().hour() << ':'
                    << std::setw(2) << m.time().min() << ':'
                    << std::setw(2) << m.time().sec() << "."
                    << std::setw(3) << m.time().usec() / 1000
                    << std::setfill(' ')
                    << ' '
                    << std::setw(7) << google::GetLogSeverityName(m.severity())
                    << ' '
                    << std::setw(5) << m.thread_id()
                    << " -- ["
                    << std::setw(24) << file_line
                    << "] ["
                    <<  std::setw(15) << probe::thread::name().substr(0, 15) << "]:";
            }
        );
        // clang-format on

        FLAGS_log_dir                   = log_dir;
        FLAGS_max_log_size              = 32; // MB
        FLAGS_stop_logging_if_full_disk = true;
        FLAGS_logbufsecs                = 0;  // s
        FLAGS_colorlogtostderr          = true;
        google::SetStderrLogging(google::GLOG_INFO);
        google::SetLogDestination(google::GLOG_INFO, std::string{ log_dir + "/SCANNER-I-" }.c_str());
        google::SetLogDestination(google::GLOG_WARNING, std::string{ log_dir + "/SCANNER-W-" }.c_str());
        google::SetLogDestination(google::GLOG_ERROR, std::string{ log_dir + "/SCANNER-E-" }.c_str());
        google::SetLogDestination(google::GLOG_FATAL, std::string{ log_dir + "/SCANNER-F-" }.c_str());
        google::SetLogFilenameExtension(".log");
        google::EnableLogCleaner(std::chrono::days(3)); // days
        google::InstallFailureSignalHandler();
        google::InstallFailureWriter(
            [](const char *data, size_t size) { LOG(ERROR) << std::string(data, size); });
    }
};

#define NOTNULL(VAL) CHECK_NOTNULL(VAL)

// FIXME: logf conflict with <cmath>
#define logd(FMT, ...) DLOG(INFO) << fmt::format(FMT, ##__VA_ARGS__)
#define logi(FMT, ...) LOG(INFO) << fmt::format(FMT, ##__VA_ARGS__)
#define logw(FMT, ...) LOG(WARNING) << fmt::format(FMT, ##__VA_ARGS__)
#define loge(FMT, ...) LOG(ERROR) << fmt::format(FMT, ##__VA_ARGS__)

#define logd_if(C, FMT, ...) DLOG_IF(INFO, C) << fmt::format(FMT, ##__VA_ARGS__)
#define logi_if(C, FMT, ...) LOG_IF(INFO, C) << fmt::format(FMT, ##__VA_ARGS__)
#define logw_if(C, FMT, ...) LOG_IF(WARNING, C) << fmt::format(FMT, ##__VA_ARGS__)
#define loge_if(C, FMT, ...) LOG_IF(ERROR, C) << fmt::format(FMT, ##__VA_ARGS__)

#endif //! SCANNER_LOGGING_H

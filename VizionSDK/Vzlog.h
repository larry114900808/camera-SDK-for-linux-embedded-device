#pragma once
#include <string>
#include <iostream>
#include <stdarg.h>
#include "spdlog/spdlog.h"
#include "spdlog/async.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/msvc_sink.h"

enum class VzLogSink
{
	VZ_LOG_SINK_CONSOLE,
	VZ_LOG_SINK_BASIC_FILE,
	VZ_LOG_SINK_DAILY,
	VZ_LOG_SINK_ROTATING,
};

enum class VzLogLevel
{
	VZ_LOG_LEVEL_TRACE,
	VZ_LOG_LEVEL_DEBUG,
	VZ_LOG_LEVEL_INFO,
	VZ_LOG_LEVEL_WARN,
	VZ_LOG_LEVEL_ERROR,
	VZ_LOG_LEVEL_CRITICAL,
	VZ_LOG_LEVEL_OFF,
};

inline bool IsFileExists(const std::string& name) {
	FILE* file;
	if (fopen_s(&file, name.c_str(), "r") == 0) {
		fclose(file);
		return true;
	}
	else {
		return false;
	}
}

class VzLog {
private:
	const std::string logpath = "logs/";
	const std::string logconfig = "vzcfg.ini";
	std::string _logname;
	char cslogpath[256];

	std::shared_ptr<spdlog::sinks::wincolor_stdout_sink_mt> console_sink;
	std::shared_ptr<spdlog::sinks::basic_file_sink_mt> file_sink;
	std::shared_ptr<spdlog::sinks::rotating_file_sink_mt> rotating_sink;
	std::shared_ptr<spdlog::sinks::msvc_sink_mt> msvc_sink;
	std::shared_ptr <spdlog::async_logger> logger;
	char msbuf[4096];

public:
	VzLog(std::string logname)
	{
		_logname = logname;
		spdlog::init_thread_pool(8192, 1);
		msvc_sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
		console_sink = std::make_shared<spdlog::sinks::wincolor_stdout_sink_mt>();
		if (IsFileExists(logpath + logconfig))
		{
			GetPrivateProfileStringA("VzLog", "LogPath", "", cslogpath, 256, (logpath + logconfig).c_str());
			rotating_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(cslogpath + logname, 1024 * 1024 * 10, 3);
			std::vector<spdlog::sink_ptr> sinks{ console_sink, rotating_sink, msvc_sink };
			logger = std::make_shared<spdlog::async_logger>("VzLog", sinks.begin(), sinks.end(), spdlog::thread_pool(), spdlog::async_overflow_policy::block);
		}
		else
		{
			std::vector<spdlog::sink_ptr> sinks{ console_sink, msvc_sink };
			logger = std::make_shared<spdlog::async_logger>("VzLog", sinks.begin(), sinks.end(), spdlog::thread_pool(), spdlog::async_overflow_policy::block);
		}
		spdlog::register_logger(logger);
	}

	~VzLog() {};

	void SetLogLevel(VzLogLevel level)
	{
		switch (level)
		{
		case VzLogLevel::VZ_LOG_LEVEL_TRACE:
			logger->set_level(spdlog::level::trace);
			break;
		case VzLogLevel::VZ_LOG_LEVEL_DEBUG:
			logger->set_level(spdlog::level::debug);
			break;
		case VzLogLevel::VZ_LOG_LEVEL_INFO:
			logger->set_level(spdlog::level::info);
			break;
		case VzLogLevel::VZ_LOG_LEVEL_WARN:
			logger->set_level(spdlog::level::warn);
			break;
		case VzLogLevel::VZ_LOG_LEVEL_ERROR:
			logger->set_level(spdlog::level::err);
			break;
		case VzLogLevel::VZ_LOG_LEVEL_CRITICAL:
			logger->set_level(spdlog::level::critical);
			break;
		default:
			logger->set_level(spdlog::level::off);
		}
	}

	void Info(const char* messeges, ...)
	{
		va_list aptr;
		va_start(aptr, messeges);
		vsprintf_s(msbuf, messeges, aptr);
		va_end(aptr);

		logger->info(msbuf);
		logger->flush();
	}
	void Warn(const char* messeges, ...)
	{
		va_list aptr;
		va_start(aptr, messeges);
		vsprintf_s(msbuf, messeges, aptr);
		va_end(aptr);

		logger->warn(msbuf);
		logger->flush();
	}
	void Debug(const char* messeges, ...)
	{
		va_list aptr;
		va_start(aptr, messeges);
		vsprintf_s(msbuf, messeges, aptr);
		va_end(aptr);

		logger->debug(msbuf);
		logger->flush();
	}
	void Error(const char* messeges, ...)
	{
		va_list aptr;
		va_start(aptr, messeges);
		vsprintf_s(msbuf, messeges, aptr);
		va_end(aptr);

		logger->error(msbuf);
		logger->flush();
	}
	void Critical(const char* messeges, ...)
	{
		va_list aptr;
		va_start(aptr, messeges);
		vsprintf_s(msbuf, messeges, aptr);
		va_end(aptr);

		logger->critical(msbuf);
		logger->flush();
	}
	void Trace(const char* messeges, ...)
	{
		va_list aptr;
		va_start(aptr, messeges);
		vsprintf_s(msbuf, messeges, aptr);
		va_end(aptr);

		logger->trace(msbuf);
		logger->flush();
	}

};

//logger that simply print using iostream cout
//based on the RNBO_LoggerNoStdLib by jb
#include "RNBO_Common.h"
#include <iostream>
#include <array>

namespace {
	std::string levelString(RNBO::LogLevel l) {
		switch (l) {
			case RNBO::LogLevel::Info:
				return "INFO";
			case RNBO::LogLevel::Warning:
				return "WARNING";
			case RNBO::LogLevel::Error:
					return "ERROR";
			default:
				return "UNKNOWN";
		}
	}
}

namespace RNBO {
	Logger consoleInstance;
	LoggerInterface* console = &consoleInstance;

	Logger::Logger() : _outputCallback(&Logger::defaultLogOutputFunction) { }
	Logger::~Logger() { }

	void Logger::setLoggerOutputCallback(OutputCallback* callback) {
		_outputCallback = callback ? callback : defaultLogOutputFunction;
	}

	void Logger::defaultLogOutputFunction(LogLevel level, const char* message) {
		std::cout << levelString(level) << "\t" << message << std::endl;
	}

	Logger& Logger::getInstance() {
		return consoleInstance;
	}
}

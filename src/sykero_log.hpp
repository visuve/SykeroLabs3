#pragma once

#include "sykero_mem.hpp"

namespace sl::log
{
	class facility final
	{
	public:
		facility(int facility, const char* path);
		~facility();
		SL_NON_COPYABLE(facility);
	};

	extern "C" void syslog(int, const char*, ...);

	template <size_t N, typename... Args>
	inline void syslog_with_source_info(
		const std::source_location& source,
		int priority,
		const char(&format)[N],
		Args... args)
	{
		char prefixed_format[N + 7] = { '%', 's', ':', '%', 'u', ':', ' ' }; // Make clang happy
		mem::join(format, prefixed_format);

		syslog(priority,
			prefixed_format,
			std::filesystem::path(source.file_name()).filename().c_str(),
			source.line(),
			args...);
	}
}

#define log_emergency(format, ...) sl::log::syslog_with_source_info(std::source_location::current(), 0, format, __VA_ARGS__)
#define log_alert(format, ...) sl::log::syslog_with_source_info(std::source_location::current(), 1, format, __VA_ARGS__)
#define log_critical(format, ...) sl::log::syslog_with_source_info(std::source_location::current(), 2, format, __VA_ARGS__)
#define log_error(format, ...) sl::log::syslog_with_source_info(std::source_location::current(), 3, format, __VA_ARGS__)
#define log_warning(format, ...) sl::log::syslog_with_source_info(std::source_location::current(), 4, format, __VA_ARGS__)
#define log_notice(format, ...) sl::log::syslog_with_source_info(std::source_location::current(), 5, format, __VA_ARGS__)
#define log_info(format, ...) sl::log::syslog_with_source_info(std::source_location::current(), 6, format, __VA_ARGS__)
#define log_debug(format, ...) sl::log::syslog_with_source_info(std::source_location::current(), 7, format, __VA_ARGS__)
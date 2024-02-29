#include "mega.pch"
#include "sykero_time.hpp"
#include "sykero_mem.hpp"

namespace sl::time
{
	std::tm local_time(const std::chrono::system_clock::time_point& time_point)
	{
		std::time_t tt = std::chrono::system_clock::to_time_t(time_point);

		std::tm tm;
		mem::clear(tm);

		if (!localtime_r(&tt, &tm))
		{
			throw std::runtime_error("std::localtime failed");
		}

		assert(tm.tm_sec >= 0 && tm.tm_hour <= 60);
		assert(tm.tm_min >= 0 && tm.tm_hour <= 59);
		assert(tm.tm_hour >= 0 && tm.tm_hour <= 23);
		assert(tm.tm_mday >= 1 && tm.tm_hour <= 31);
		assert(tm.tm_mon >= 0 && tm.tm_mon <= 11);
		assert(tm.tm_year);
		assert(tm.tm_wday >= 0 && tm.tm_wday <= 6);
		assert(tm.tm_yday >= 0 && tm.tm_yday <= 365);

		return tm;
	}

	bool is_night(const std::chrono::system_clock::time_point& time_point)
	{
		std::tm tm = local_time(time_point);
		return tm.tm_hour >= 22 || tm.tm_hour <= 8;
	}

	template <size_t FS, size_t ES>
	std::string to_string(
		const std::chrono::system_clock::time_point& time_point,
		const char(&format)[FS],
		const char(&)[ES])
	{
		std::tm tm = local_time(time_point);

		char time_stamp[ES];

		if (std::strftime(time_stamp, ES, format, &tm) != ES - 1)
		{
			throw std::runtime_error("std::strftime failed");
		}

		return { time_stamp, ES - 1 };
	}

	std::string time_string(const std::chrono::system_clock::time_point& time_point)
	{
		return to_string(time_point, "%T", "16:45:18");
	}

	std::string date_string(const std::chrono::system_clock::time_point& time_point)
	{
		return to_string(time_point, "%F", "2024-02-28");
	}

	std::string datetime_string(const std::chrono::system_clock::time_point& time_point)
	{
		return to_string(time_point, "%FT%T", "2024-02-28T16:45:18");
	}
}
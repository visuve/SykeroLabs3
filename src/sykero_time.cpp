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

	std::string to_string(const std::chrono::system_clock::time_point& time_point)
	{
		std::tm tm = local_time(time_point);

		constexpr size_t time_stamp_size = sizeof("2024-02-28T16:45:18");

		char time_stamp[time_stamp_size];

		if (std::strftime(time_stamp, time_stamp_size, "%FT%T", &tm) != time_stamp_size - 1)
		{
			throw std::runtime_error("std::strftime failed");
		}

		return { time_stamp, time_stamp_size - 1 };
	}
}
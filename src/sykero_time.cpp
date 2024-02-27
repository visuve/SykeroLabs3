#include "mega.pch"
#include "sykero_time.hpp"

namespace sl::time
{
	bool is_night(const std::chrono::system_clock::time_point& time_point)
	{
		// TODO: use zoned_time when its available in libstdc++

		std::time_t tt = std::chrono::system_clock::to_time_t(time_point);

		std::tm* tm = std::localtime(&tt);

		if (!tm)
		{
			throw std::runtime_error("std::localtime failed");
		}

		assert(tm->tm_hour >= 0 && tm->tm_hour < 24);

		return tm->tm_hour >= 22 || tm->tm_hour <= 8;
	}
}
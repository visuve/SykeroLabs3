#include "mega.pch"
#include "sykero_time.hpp"
#include "sykero_mem.hpp"
#include "sykero_log.hpp"

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

	std::chrono::hh_mm_ss<std::chrono::nanoseconds> time_to_midnight(const std::chrono::system_clock::time_point& time_point)
	{
		std::chrono::nanoseconds nanos_to_midnight = std::chrono::ceil<std::chrono::days>(time_point) - time_point;

		// TODO: use std::chrono::zoned_time when available
		nanos_to_midnight -= std::chrono::seconds(local_time(time_point).tm_gmtoff);

		return std::chrono::hh_mm_ss<std::chrono::nanoseconds>(nanos_to_midnight);
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

	std::string time_string(const std::chrono::hh_mm_ss<std::chrono::nanoseconds>& hh_mm_ss)
	{
		std::string time_stamp(0x40, '\0');
		
		// TODO: use std::format when available
		int size = std::snprintf(
			time_stamp.data(),
			time_stamp.size(),
			"%02ld:%02ld:%02ld",
			hh_mm_ss.hours().count(),
			hh_mm_ss.minutes().count(),
			hh_mm_ss.seconds().count());

		assert(size == 8);

		if (size <= 0)
		{
			throw std::runtime_error("std::sprintf failed");
		}

		time_stamp.resize(static_cast<size_t>(size));

		return time_stamp;
	}

	std::string date_string(const std::chrono::system_clock::time_point& time_point)
	{
		return to_string(time_point, "%F", "2024-02-28");
	}

	std::string datetime_string(const std::chrono::system_clock::time_point& time_point)
	{
		return to_string(time_point, "%FT%T", "2024-02-28T16:45:18");
	}

	timer::timer(
		std::function<void()> callback,
		const std::chrono::hh_mm_ss<std::chrono::nanoseconds>& start_time,
		std::chrono::nanoseconds interval) :
		_callback(callback)
	{
		create();
		start(start_time, interval);
	}

	timer::~timer()
	{
		if (_identifier)
		{
			if (timer_delete(_identifier) < 0)
			{
				log_error("timer_delete(%p) failed. Errno %d", _identifier, errno);
			}
		}

		log_info("time::timer %p destroyed.", _identifier);
	}

	void timer::create()
	{
		assert(_identifier == nullptr);

		mem::clear(_event);

		_event.sigev_notify = SIGEV_THREAD;
		_event.sigev_value.sival_ptr = this;
		_event.sigev_notify_function = notify_function;
		_event.sigev_notify_attributes = nullptr;

		if (timer_create(CLOCK_REALTIME, &_event, &_identifier) < 0)
		{
			throw std::system_error(errno, std::system_category(), "timer_create");
		}

		log_info("time::timer %p created.", _identifier);
	}

	void timer::start(const std::chrono::hh_mm_ss<std::chrono::nanoseconds>& start_time, std::chrono::nanoseconds interval)
	{
		_spec.it_interval = duration_to_timespec(interval);
		_spec.it_value = duration_to_timespec(start_time.to_duration());

		itimerspec old_spec;
		mem::clear(old_spec);

		if (timer_settime(_identifier, 0, &_spec, &old_spec) < 0)
		{
			throw std::system_error(errno, std::system_category(), "timer_settime");
		}

		assert(old_spec.it_interval.tv_sec == 0);
		assert(old_spec.it_interval.tv_nsec == 0);
		assert(old_spec.it_value.tv_sec == 0);
		assert(old_spec.it_value.tv_sec == 0);

		log_debug("time::timer %p started.", _identifier);
	}

	void timer::notify_function(sigval sv)
	{
		auto self = reinterpret_cast<timer*>(sv.sival_ptr);

		assert(self);

		if (self->_callback)
		{
			self->_callback();
		}
	}
}
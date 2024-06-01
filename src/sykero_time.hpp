#pragma once

namespace sl::time
{
	std::tm local_time(const std::chrono::system_clock::time_point& time_point = std::chrono::system_clock::now());

	bool is_night(const std::chrono::system_clock::time_point& time_point = std::chrono::system_clock::now());

	std::chrono::hh_mm_ss<std::chrono::nanoseconds> time_to_midnight(const std::chrono::system_clock::time_point& time_point = std::chrono::system_clock::now());

	std::string time_string(const std::chrono::hh_mm_ss<std::chrono::nanoseconds>& hh_mm_ss);
	std::string time_string(const std::chrono::system_clock::time_point& time_point = std::chrono::system_clock::now());
	std::string date_string(const std::chrono::system_clock::time_point& time_point = std::chrono::system_clock::now());
	std::string datetime_string(const std::chrono::system_clock::time_point& time_point = std::chrono::system_clock::now());

	template <typename Rep, typename Period>
	constexpr timespec duration_to_timespec(std::chrono::duration<Rep, Period> duration)
	{
		auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
		std::chrono::nanoseconds nanos = duration - seconds;

		return { seconds.count(), nanos.count() };
	}

	template <typename T>
	constexpr T timespec_to_duration(const timespec& spec)
	{
		auto secs = std::chrono::seconds(spec.tv_sec);
		auto nanos = std::chrono::nanoseconds(spec.tv_nsec);

		return std::chrono::duration_cast<T>(nanos + secs);
	}

	template<typename Rep, typename Period>
	std::chrono::duration<Rep, Period> nanosleep(std::chrono::duration<Rep, Period> duration)
	{
		timespec requested = duration_to_timespec(duration);

		assert(requested.tv_nsec >= 0 && requested.tv_nsec <= 999999999);

		timespec remaining = { 0, 0 };

		int result = ::nanosleep(&requested, &remaining);

		if (result < 0)
		{
			result = errno;

			if (result != EINTR)
			{
				throw std::system_error(result, std::system_category(), "nanosleep");
			}
		}

		return timespec_to_duration<std::chrono::duration<Rep, Period>>(remaining);
	}

	class timer
	{
	public:
		timer(
			std::function<void()> callback,
			const std::chrono::hh_mm_ss<std::chrono::nanoseconds>& start_time,
			std::chrono::nanoseconds interval = std::chrono::nanoseconds(0));

		~timer();

	private:
		void create();
		void start(
			const std::chrono::hh_mm_ss<std::chrono::nanoseconds>& start_time,
			std::chrono::nanoseconds interval);

		static void notify_function(sigval);

		std::function<void()> _callback;
		timer_t _identifier = nullptr;
		sigevent _event;
		itimerspec _spec;
	};
}
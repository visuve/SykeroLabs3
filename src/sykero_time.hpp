#pragma once

namespace sl::time
{
	std::tm local_time(const std::chrono::system_clock::time_point& time_point = std::chrono::system_clock::now());

	bool is_night(const std::chrono::system_clock::time_point& time_point = std::chrono::system_clock::now());

	std::string time_string(const std::chrono::system_clock::time_point& time_point = std::chrono::system_clock::now());
	std::string date_string(const std::chrono::system_clock::time_point& time_point = std::chrono::system_clock::now());
	std::string datetime_string(const std::chrono::system_clock::time_point& time_point = std::chrono::system_clock::now());

	template<typename Rep, typename Period>
	std::chrono::duration<Rep, Period> nanosleep(const std::chrono::duration<Rep, Period>& time)
	{
		auto secs = std::chrono::duration_cast<std::chrono::seconds>(time);
		auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(time - secs);

		timespec requested;
		requested.tv_sec = secs.count();
		requested.tv_nsec = nanos.count();

		assert(requested.tv_nsec >= 0 && requested.tv_nsec <= 999999999);

		timespec remaining;
		remaining.tv_sec = 0;
		remaining.tv_nsec = 0;

		int result = ::nanosleep(&requested, &remaining);

		if (result < 0)
		{
			result = errno;

			if (result != EINTR)
			{
				throw std::system_error(result, std::system_category(), "nanosleep");
			}
		}

		secs = std::chrono::seconds(remaining.tv_sec);
		nanos = std::chrono::nanoseconds(remaining.tv_nsec);

		// Cast back to the accuracy the request was made
		return std::chrono::duration_cast<std::chrono::duration<Rep, Period>>(nanos + secs);
	}

	class timer
	{
	public:
		timer(
			std::function<void()> callback,
			const std::chrono::system_clock::time_point& start_time,
			std::chrono::seconds interval = std::chrono::seconds(0));

		~timer();

		void start();
		void stop();

	private:
		static void notify_function(sigval);

		std::function<void()> _callback;
		timer_t _identifier = nullptr;
		sigevent _event;
		itimerspec _spec;
	};
}
#pragma once

#include "sykero_mem.hpp"

namespace sl::io
{
	class file_descriptor
	{
	public:
		file_descriptor() = default;
		file_descriptor(int descriptor);
		file_descriptor(const std::filesystem::path& path, int mode = O_RDONLY);
		SL_NON_COPYABLE(file_descriptor);
		virtual ~file_descriptor();

		void open(const std::filesystem::path& path, int mode);
		void close(bool sync = true);

		bool read(void* data, size_t size) const;
		bool read_text(std::string& text) const;

		template <typename T>
		bool read_value(T& t) const
		{
			return file_descriptor::read(&t, sizeof(T));
		}

		void write(const void* data, size_t size) const;
		void write_text(std::string_view text) const;

		template <typename T>
		void write_value(const T& t) const
		{
			return file_descriptor::write(&t, sizeof(T));
		}

		void fsync() const;
		size_t file_size() const;
		void lseek(off_t offset, int whence) const;

	protected:
		template<typename... Args>
		int ioctl(uint32_t request, Args... args) const
		{
			int result = ::ioctl(_descriptor, request, args...);

			if (result < 0)
			{
				throw std::system_error(errno, std::system_category(), "ioctl");
			}

			return result;
		}

		template<typename Rep, typename Period>
		bool poll(std::chrono::duration<Rep, Period> timeout, uint16_t events) const
		{
			pollfd poll_descriptor;
			poll_descriptor.fd = _descriptor;
			poll_descriptor.events = events;
			poll_descriptor.revents = 0;

			int ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();

			int result = ::poll(&poll_descriptor, 1, ms);

			if (result < 0)
			{
				throw std::system_error(errno, std::system_category(), "poll");
			}

			return result > 0;
		}

		int _descriptor = 0;
	};

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
}
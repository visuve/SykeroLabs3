#pragma once

namespace sl
{
	class file_descriptor
	{
	public:
		file_descriptor() = default;
		file_descriptor(int descriptor);
		file_descriptor(const std::filesystem::path& path, int mode = O_RDONLY);
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

		void poll(std::chrono::milliseconds timeout) const;

		int _descriptor = 0;
	};

	template<typename Rep, typename Period>
	std::chrono::nanoseconds nanosleep(const std::chrono::duration<Rep, Period>& time)
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

			if (result != -EINTR)
			{
				throw std::system_error(result, std::system_category(), "nanosleep");
			}
		}

		return std::chrono::seconds(remaining.tv_sec) + std::chrono::nanoseconds(remaining.tv_sec);
	}
}
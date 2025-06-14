#pragma once

#include "sykero_mem.hpp"

namespace sl::io
{
	class file_descriptor
	{
	public:
		file_descriptor() = default;
		explicit file_descriptor(int descriptor);
		file_descriptor(const std::filesystem::path& path, int flags = O_RDONLY);
		SL_NON_COPYABLE(file_descriptor);
		virtual ~file_descriptor();

		void open(const std::filesystem::path& path, int flags);

		size_t read(void* data, size_t size) const;

		template <size_t N>
		size_t read_text(char(&text)[N]) const
		{
			return read(text, N);
		}

		inline size_t read_text(std::string& text) const
		{
			return read(text.data(), text.size());
		}

		template <typename T>
		bool read_value(T& t) const
		{
			return file_descriptor::read(&t, sizeof(T)) == sizeof(T);
		}

		void write(const void* data, size_t size) const;
		
		template <size_t N>
		void write_text(const char(&text)[N]) const
		{
			return write(text, N);
		}

		inline void write_text(const std::string& text) const
		{
			return write(text.data(), text.size());
		}

		template <typename T>
		void write_value(const T& t) const
		{
			return file_descriptor::write(&t, sizeof(T));
		}

		size_t file_size() const;

		void reposition(off_t offset) const;

	protected:
		off_t lseek(off_t offset, int whence) const;

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

		struct stat fstat() const;

		void fsync() const;

	private:
		int _descriptor = 0;
		__mode_t _mode = 0;

		void close();
	};

	template<typename T>
	concept Arithmetic = std::is_arithmetic_v<T>;
	template <Arithmetic T>
	T value_from_file(const io::file_descriptor& file)
	{
		char buffer[0x40];

		size_t bytes_read = file.read_text(buffer);

		if (!bytes_read)
		{
			throw std::runtime_error("failed to read");
		}

		file.reposition(0);

		T value;
		std::from_chars_result result = std::from_chars(buffer, buffer + bytes_read, value);

		if (result.ec != std::errc())
		{
			throw std::runtime_error("failed to convert value from file");
		}

		return value;
	}
}
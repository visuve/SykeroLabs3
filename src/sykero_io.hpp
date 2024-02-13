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

	private:
		int _descriptor = 0;
	};
}
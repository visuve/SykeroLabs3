#include "mega.pch"
#include "sykero_mem.hpp"
#include "sykero_io.hpp"
#include "sykero_log.hpp"

namespace sl::io
{
	file_descriptor::file_descriptor(int descriptor) :
		_descriptor(descriptor)
	{
		if (_descriptor < 0)
		{
			// I do now know if errno is set
			throw std::invalid_argument("Invalid descriptor");
		}
	}

	file_descriptor::file_descriptor(const std::filesystem::path& path, int mode) :
		_descriptor(0)
	{
		open(path, mode);
	}

	file_descriptor::~file_descriptor()
	{
		if (_descriptor > 0)
		{
			if (::close(_descriptor) < 0)
			{
				log_error("close(%d) failed. Errno %d", _descriptor, errno);
			}
		}
	}

	void file_descriptor::open(const std::filesystem::path& path, int mode)
	{
		file_descriptor::close();

		_descriptor = ::open(path.c_str(), mode);

		if (_descriptor < 0)
		{
			throw std::system_error(errno, std::system_category(), path.c_str());
		}
	}

	void file_descriptor::close(bool sync)
	{
		if (_descriptor <= 0)
		{
			return;
		}

		if (sync)
		{
			file_descriptor::fsync();
		}

		int result = ::close(_descriptor);

		_descriptor = 0;

		if (result < 0)
		{
			throw std::system_error(errno, std::system_category(), "close");
		}
	}

	bool file_descriptor::read(void* data, size_t size) const
	{
		int result = ::read(_descriptor, data, size);

		if (result == 0)
		{
			return false;
		}

		if (result < 0)
		{
			throw std::system_error(errno, std::system_category(), "read");
		}

		if (static_cast<size_t>(result) != size)
		{
			throw std::system_error(EIO, std::system_category(), "read");
		}

		return true;
	}

	bool file_descriptor::read_text(std::string& text) const
	{
		return file_descriptor::read(text.data(), text.size());
	}

	void file_descriptor::write(const void* data, size_t size) const
	{
		int result = ::write(_descriptor, data, size);

		if (result < 0)
		{
			throw std::system_error(errno, std::system_category(), "write");
		}

		if (static_cast<size_t>(result) != size)
		{
			throw std::system_error(-EIO, std::system_category(), "write");
		}
	}

	void file_descriptor::write_text(std::string_view text) const
	{
		return file_descriptor::write(text.data(), text.size());
	}

	void file_descriptor::fsync() const
	{
		if (::fsync(_descriptor) < 0)
		{
			throw std::system_error(errno, std::system_category(), "fsync");
		}
	}

	size_t file_descriptor::file_size() const
	{
		struct stat buffer;
		mem::clear(buffer);

		if (::fstat(_descriptor, &buffer) < 0)
		{
			throw std::system_error(errno, std::system_category(), "fstat");
		}

		return buffer.st_size;
	}

	void file_descriptor::lseek(off_t offset, int whence) const
	{
		if (::lseek(_descriptor, offset, whence) < 0)
		{
			throw std::system_error(errno, std::system_category(), "lseek");
		}
	}
}
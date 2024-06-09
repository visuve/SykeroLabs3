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
			throw std::invalid_argument("invalid descriptor");
		}

		_mode = file_descriptor::fstat().st_mode;
	}

	file_descriptor::file_descriptor(const std::filesystem::path& path, int flags) :
		_descriptor(0),
		_mode(0)
	{
		file_descriptor::open(path, flags);
	}

	file_descriptor::~file_descriptor()
	{
		if (_descriptor > 0)
		{
			if (S_ISREG(_mode) && ::fsync(_descriptor) < 0)
			{
				log_error("fsync(%d) failed; errno %d.", _descriptor, errno);
			}

			if (::close(_descriptor) < 0)
			{
				log_error("close(%d) failed; errno %d.", _descriptor, errno);
			}
		}
	}

	void file_descriptor::open(const std::filesystem::path& path, int flags)
	{
		file_descriptor::close();

		if ((flags & O_CREAT) == O_CREAT)
		{
			constexpr mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
			_descriptor = ::open(path.c_str(), flags, mode);
		}
		else
		{
			_descriptor = ::open(path.c_str(), flags);
		}

		if (_descriptor < 0)
		{
			throw std::system_error(errno, std::system_category(), path.c_str());
		}

		_mode = file_descriptor::fstat().st_mode;
	}

	size_t file_descriptor::read(void* data, size_t size) const
	{
		int result = ::read(_descriptor, data, size);

		if (result < 0)
		{
			throw std::system_error(errno, std::system_category(), "read");
		}

		return static_cast<size_t>(result);
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

	size_t file_descriptor::file_size() const
	{
		return file_descriptor::fstat().st_size;
	}

	void file_descriptor::reposition(off_t offset) const
	{
		if (file_descriptor::lseek(offset, SEEK_SET) != offset)
		{
			throw std::runtime_error("failed to reposition");
		}
	}

	off_t file_descriptor::lseek(off_t offset, int whence) const
	{
		off_t result = ::lseek(_descriptor, offset, whence);

		if (result < 0)
		{
			throw std::system_error(errno, std::system_category(), "lseek");
		}

		return result;
	}

	struct stat file_descriptor::fstat() const
	{
		struct stat buffer;
		mem::clear(buffer);

		if (::fstat(_descriptor, &buffer) < 0)
		{
			throw std::system_error(errno, std::system_category(), "fstat");
		}

		return buffer;
	}

	void file_descriptor::fsync() const
	{
		if (!S_ISREG(_mode))
		{
			return;
		}

		if (::fsync(_descriptor) < 0)
		{
			throw std::system_error(errno, std::system_category(), "fsync");
		}
	}

	void file_descriptor::close()
	{
		if (_descriptor <= 0)
		{
			return;
		}

		file_descriptor::fsync();

		int result = ::close(_descriptor);

		_descriptor = 0;

		if (result < 0)
		{
			throw std::system_error(errno, std::system_category(), "close");
		}
	}
}
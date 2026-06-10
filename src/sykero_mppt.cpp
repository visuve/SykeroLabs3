#include "mega.pch"
#include "sykerolabs.hpp"
#include "sykero_mppt.hpp"
#include "sykero_log.hpp"

namespace sl::mppt
{
	constexpr char DELIM_LF = '\n';
	constexpr char DELIM_CR = '\r';
	constexpr char DELIM_TAB = '\t';

	constexpr char KEY_CHECKSUM[] = "Checksum";

	controller::controller(const std::filesystem::path& path) :
		io::file_descriptor(path, O_RDWR | O_NOCTTY | O_NDELAY),
		_properties(std::to_array<std::pair<std::string, property*>>(
		{
			{ "V", &battery_voltage },
			{ "I", &battery_current },
			{ "VPV", &panel_voltage },
			{ "PPV", &panel_power },
			{ "IL", &load_current },
			{ "CS", &state },
			{ "ERR", &error },
			{ "H19", &yield_total },
			{ "H21", &max_power_today }
		}))
	{
		_key.reserve(MAX_SERIAL_STRING_LENGTH);
		_value.reserve(MAX_SERIAL_STRING_LENGTH);

		struct termios options;
		mem::clear(options);

		if (ioctl(TCGETS, &options) < 0)
		{
			throw std::runtime_error("failed to get termios");
		}

		cfsetispeed(&options, B19200);
		cfsetospeed(&options, B19200);

		options.c_cflag |= (CLOCAL | CREAD | CS8);
		options.c_cflag &= ~(PARENB | CSTOPB | CSIZE);
		options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
		options.c_oflag &= ~OPOST;

		if (ioctl(TCSETS, &options) < 0)
		{
			throw std::runtime_error("failed to set termios");
		}
	}

	std::span<uint8_t> controller::read_serial(std::span<uint8_t> buffer)
	{
		if (!poll(std::chrono::milliseconds(100), POLLIN))
		{
			return std::span<uint8_t>();
		}

		size_t bytes_read = read(buffer.data(), buffer.size());

		return { buffer.data(), bytes_read };
	}

	bool controller::parse(std::span<uint8_t> data)
	{
		for (const auto& byte : data)
		{
			switch (advance(byte))
			{
				case frame_event::PENDING:
				{
					break;
				}
				case frame_event::PAIR_READY:
				{
					parse_pair();
					break;
				}
				case frame_event::BLOCK_READY:
				{
					commit_block();
					return true;
				}
				case frame_event::CHECKSUM_MISMATCH:
				{
					undo_block();
					break;
				}
			}
		}

		return false;
	}

	controller::frame_event controller::advance(uint8_t byte)
	{
		switch (_state)
		{
			case frame_state::HEADER:
				return handle_header_byte(byte);
			case frame_state::KEY:
				return handle_key_byte(byte);
			case frame_state::VALUE:
				return handle_value_byte(byte);
			case frame_state::CHECKSUM:
				return handle_checksum_byte(byte);
			case frame_state::DISCARD:
				return handle_discard(byte);
		}

		return frame_event::PENDING;
	}


	controller::frame_event controller::handle_header_byte(uint8_t byte)
	{
		if (byte != DELIM_CR && byte != DELIM_LF)
		{
			_state = frame_state::KEY;
			_checksum = byte;
			_key.clear();
			_key.push_back(static_cast<char>(byte));
			_value.clear();
		}

		return frame_event::PENDING;
	}

	controller::frame_event controller::handle_key_byte(uint8_t byte)
	{
		_checksum += byte;

		if (byte == DELIM_TAB)
		{
			if (_key == KEY_CHECKSUM)
			{
				_state = frame_state::CHECKSUM;
			}
			else
			{
				_state = frame_state::VALUE;
				_value.clear();
			}
		}
		else if (byte != DELIM_CR)
		{
			_key.push_back(static_cast<char>(byte));

			if (_key.length() > MAX_SERIAL_STRING_LENGTH)
			{
				_state = frame_state::DISCARD;
			}
		}

		return frame_event::PENDING;
	}

	controller::frame_event controller::handle_value_byte(uint8_t byte)
	{
		_checksum += byte;

		if (byte == DELIM_LF)
		{
			_state = frame_state::KEY;
			return frame_event::PAIR_READY;
		}
		
		if (byte != DELIM_CR)
		{
			_value.push_back(static_cast<char>(byte));

			if (_value.length() > MAX_SERIAL_STRING_LENGTH)
			{
				_state = frame_state::DISCARD;
			}
		}

		return frame_event::PENDING;
	}

	controller::frame_event controller::handle_checksum_byte(uint8_t byte)
	{
		_checksum += byte;
		_state = frame_state::HEADER;

		if (_checksum != 0)
		{
			_checksum = 0;
			return frame_event::CHECKSUM_MISMATCH;
		}

		return frame_event::BLOCK_READY;
	}

	controller::frame_event controller::handle_discard(uint8_t byte)
	{
		if (byte == DELIM_LF)
		{
			undo_block();
		}

		return frame_event::PENDING;
	}

	void controller::parse_pair()
	{
		for (const auto& [key, value] : _properties)
		{
			if (key == _key)
			{
				value->parse(_value);
				_key.clear();
				_value.clear();
				return;
			}
		}
	}

	void controller::commit_block()
	{
		for (const auto& [_, value] : _properties)
		{
			value->commit();
		}

		reset();
	}

	void controller::undo_block()
	{
		for (const auto& [_, value] : _properties)
		{
			value->undo();
		}

		reset();
	}

	void controller::reset()
	{
		_state = frame_state::HEADER;
		_checksum = 0;
		_key.clear();
		_value.clear();
	}
}
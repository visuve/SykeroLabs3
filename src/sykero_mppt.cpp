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
		io::file_descriptor(path, O_RDONLY | O_NOCTTY | O_NDELAY),
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

		// get existing options
		termios options = tcgetattr();

		// make raw not to omit any bytes
		cfmakeraw(&options);

		// set speed for write too to make sure options is well-formed
		cfsetispeed(&options, B19200);
		cfsetospeed(&options, B19200);

		// 8 data bits, no parity, 1 stop bit
		options.c_cflag &= ~PARENB;
		options.c_cflag &= ~CSTOPB;
		options.c_cflag &= ~CSIZE;
		options.c_cflag |= CS8;

		// enable receiver and ignore modem control lines
		options.c_cflag |= CREAD | CLOCAL;

		// blocking read configuration
		options.c_cc[VMIN] = 1;
		options.c_cc[VTIME] = 0;

		// set modified options
		tcsetattr(options);

		// flush input to discard any partial frames
		// that may have been received before the options were set
		tcflush(TCIFLUSH);
	}

	std::span<uint8_t> controller::read_serial(std::span<uint8_t> buffer)
	{
		if (!poll(std::chrono::milliseconds(100), POLLIN))
		{
			return std::span<uint8_t>();
		}

		size_t bytes_read = read(buffer.data(), buffer.size());

		if (!bytes_read)
		{
			return std::span<uint8_t>();
		}

		return { buffer.data(), bytes_read };
	}

	bool controller::parse(std::span<const uint8_t> data)
	{
		bool block_ready = false;

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
					block_ready = true;
					break;
				}
				case frame_event::BLOCK_INVALID:
				{
					undo_block();
					break;
				}
			}
		}

		return block_ready;
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
		_checksum += byte;

		if (byte != DELIM_CR && byte != DELIM_LF)
		{
			_state = frame_state::KEY;
			_key.clear();
			_key.push_back(static_cast<char>(byte));
			_value.clear();
			_block_counter++;
			log_debug("started parsing block #%zu", _block_counter);
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

		if (_checksum != 0)
		{
			log_warning("checksum mismatch in block #%zu", _block_counter);
			return frame_event::BLOCK_INVALID;
		}

		return frame_event::BLOCK_READY;
	}

	controller::frame_event controller::handle_discard(uint8_t byte)
	{
		if (byte == DELIM_LF)
		{
			log_debug("delimiter error in block #%zu", _block_counter);
			return frame_event::BLOCK_INVALID;
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
				break;
			}
		}

		_key.clear();
		_value.clear();
	}

	void controller::commit_block()
	{
		for (const auto& [_, value] : _properties)
		{
			value->commit();
		}

		log_debug("parsed block #%zu", _block_counter);
		reset();
	}

	void controller::undo_block()
	{
		for (const auto& [_, value] : _properties)
		{
			value->undo();
		}

		log_debug("discarded block #%zu", _block_counter);
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
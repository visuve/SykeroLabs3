#include "mega.pch"

namespace sl
{
	struct pins
	{
		enum : uint8_t
		{
			RESERVOIR_PUMP_1 = 5,
			RESERVOIR_PUMP_2 = 6,
			NFT_PUMP_1 = 13,
			NFT_PUMP_2 = 16,
			FAN_PWM_SIGNAL = 12,
			FAN_1_RELAY = 19,
			FAN_2_RELAY = 20,
			FAN_1_TACHOMETER = 23,
			FAN_2_TACHOMETER = 24
		};
	};

	volatile std::sig_atomic_t signaled;

	void signal_handler(int signal)
	{
		signaled = signal;
	}

	class raii_descriptor
	{
	public:
		raii_descriptor(int descriptor) :
			_descriptor(descriptor)
		{
			if (_descriptor < 0)
			{
				throw std::system_error(errno, std::system_category(), "open");
			}
		}

		raii_descriptor(std::string_view path, int mode = O_RDONLY) :
			raii_descriptor(open(path.data(), mode))
		{
		}

		inline virtual ~raii_descriptor()
		{
			if (_descriptor > 0)
			{
				close(_descriptor);
			}
		}

		bool read(void* data, size_t size) const
		{
			int result = ::read(_descriptor, data, size);

			if (result < 0)
			{
				if (errno == -EAGAIN)
				{
					return false; // No data
				}

				throw std::system_error(errno, std::system_category(), "read");
			}

			if (result != size)
			{
				throw std::system_error(-EIO, std::system_category(), "read");
			}

			return true;
		}

		template <typename T>
		bool read_value(T& t) const
		{
			return raii_descriptor::read(&t, sizeof(T));
		}

		void lseek(off_t offset, int whence) const
		{
			int result = ::lseek(_descriptor, offset, whence);

			if (result < 0)
			{
				throw std::system_error(errno, std::system_category(), "lseek");
			}
		}

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

		void poll(std::chrono::milliseconds timeout) const
		{
			pollfd poll_descriptor;
			poll_descriptor.fd = _descriptor;
			poll_descriptor.events = POLLPRI | POLLERR;
			poll_descriptor.revents = 0;

			int result = ::poll(&poll_descriptor, 1, timeout.count());

			if (result < 0)
			{
				throw std::system_error(errno, std::system_category(), "poll");
			}
		}

		int _descriptor = 0;
	};

	struct gpio_lvp // line value pair
	{
		uint32_t offset = 0;
		bool value = true;
	};

	class gpio_line_group : private raii_descriptor
	{
	public:
		gpio_line_group(int descriptor, const std::set<uint32_t>& offsets) :
			raii_descriptor(descriptor),
			_offsets(offsets)
		{
		}

		void read_values(std::span<gpio_lvp> data) const
		{
			assert(data.size() <= _offsets.size());

			std::bitset<64> mask;
			std::bitset<64> bits;

			for (const gpio_lvp& lvp : data)
			{
				size_t i = offset_to_index(lvp.offset);
				mask.set(i, true);
			}

			gpio_v2_line_values values = { 0, mask.to_ullong() };
			raii_descriptor::ioctl(GPIO_V2_LINE_GET_VALUES_IOCTL, &values);
			bits = values.bits;

			for (gpio_lvp& lvp : data)
			{
				size_t i = offset_to_index(lvp.offset);
				lvp.value = bits[i];
			}
		}

		void read_value(gpio_lvp& lvp) const
		{
			gpio_lvp data[] = { lvp };
			read_values(data);
			lvp = data[0];
		}

		void write_values(std::span<gpio_lvp> data) const
		{
			assert(data.size() <= _offsets.size());

			std::bitset<64> mask;
			std::bitset<64> bits;

			for (const gpio_lvp& lvp : data)
			{
				size_t i = offset_to_index(lvp.offset);
				mask.set(i, true);
				bits.set(i, lvp.value);
			}

			gpio_v2_line_values values = { bits.to_ullong(), mask.to_ullong() };
			raii_descriptor::ioctl(GPIO_V2_LINE_SET_VALUES_IOCTL, &values);
		}

		// https://github.com/torvalds/linux/blob/master/tools/gpio/gpio-event-mon.c
		gpio_v2_line_event read_event() const
		{
			gpio_v2_line_event event = { 0 };
			raii_descriptor::read_value(event);
			return event;
		}

		void write_value(const gpio_lvp& lvp) const
		{
			gpio_lvp data[] = { lvp };
			write_values(data);
		}

	private:
		inline size_t offset_to_index(uint32_t offset) const
		{
			auto iter = std::find(_offsets.cbegin(), _offsets.cend(), offset);

			if (iter == _offsets.cend())
			{
				throw std::invalid_argument("Offset not found");
			}

			return std::distance(_offsets.cbegin(), iter);
		}

		std::set<uint32_t> _offsets;
	};

	// https://github.com/torvalds/linux/blob/master/tools/gpio/lsgpio.c
	// https://github.com/torvalds/linux/blob/master/tools/gpio/gpio-utils.c
	class gpio_chip : public raii_descriptor
	{
	public:
		inline gpio_chip(std::string_view name) :
			raii_descriptor(name)
		{
			if (_descriptor < 0)
			{
				throw std::system_error(errno, std::system_category(), name.data());
			}
		}

		gpio_line_group lines(uint64_t flags, const std::set<uint32_t>& offsets) const
		{
			gpio_v2_line_config config = { 0 };
			config.flags = flags;

			gpio_v2_line_request request = { 0 };
			request.config = config;
			request.num_lines = offsets.size();

			size_t i = 0;

			for (uint32_t offset : offsets)
			{
				request.offsets[i] = offset;
				++i;
			}
			
			clone(request.consumer, "sykerolabs");

			raii_descriptor::ioctl(GPIO_V2_GET_LINE_IOCTL, &request);

			return gpio_line_group(request.fd, offsets);
		}
	};

	int run()
	{
		const std::set<uint32_t> input_pins =
		{
			pins::RESERVOIR_PUMP_1,
			pins::RESERVOIR_PUMP_2
		};

		const std::set<uint32_t> output_pins =
		{
			pins::NFT_PUMP_1,
			pins::NFT_PUMP_2,
			pins::FAN_1_RELAY,
			pins::FAN_2_RELAY
		};

		const std::set<uint32_t> monitor_pins =
		{
			pins::FAN_1_TACHOMETER,
			pins::FAN_2_TACHOMETER
		};

		constexpr char chip_name[] = "/dev/gpiochip4";

		gpio_chip chip(chip_name);

		gpio_line_group input_lines =
			chip.lines(GPIO_V2_LINE_FLAG_INPUT, input_pins);

		gpio_line_group output_lines =
			chip.lines(GPIO_V2_LINE_FLAG_OUTPUT, output_pins);

		//gpio_line_group monitor_lines =
		//	chip.lines(GPIO_V2_LINE_FLAG_EDGE_RISING, monitor_pins);

		uint64_t t = 0;

		raii_descriptor thermal_zone0("/sys/class/thermal/thermal_zone0/temp");

		std::string temperature(5, '\0');

		while (!signaled)
		{
			std::cout << "\nT=" << ++t << ": \n";

			std::array<gpio_lvp, 2> input_data =
			{
				gpio_lvp(pins::RESERVOIR_PUMP_1),
				gpio_lvp(pins::RESERVOIR_PUMP_2)
			};

			input_lines.read_values(input_data);

			for (auto& input : input_data)
			{
				std::cout << input.offset << '=' << input.value << '\n';
			}			

			std::array<gpio_lvp, 4> output_data =
			{
				gpio_lvp(pins::NFT_PUMP_1, t % 4 == 0),
				gpio_lvp(pins::NFT_PUMP_2, t % 4 == 1),
				gpio_lvp(pins::FAN_1_RELAY, t % 4 == 2),
				gpio_lvp(pins::FAN_2_RELAY, t % 4 == 3)
			};

			output_lines.write_values(output_data);

			thermal_zone0.read(temperature.data(), temperature.size());
			thermal_zone0.lseek(0, SEEK_SET);

			std::cout << "CPU @ " << std::stof(temperature) / 1000.0f << "c\n";

			std::this_thread::sleep_for(std::chrono::seconds(1));
		}

		return 0;
	}
}

int main()
{
	std::signal(SIGINT, sl::signal_handler);

	try
	{
		std::cout << "Starting...\n";
		return sl::run();
	}
	catch (const std::system_error& e)
	{
		std::cout << e.what() << std::endl;
	}

	return -1;
}
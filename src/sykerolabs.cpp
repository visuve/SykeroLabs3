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
		raii_descriptor() = default;

		raii_descriptor(int descriptor) :
			_descriptor(descriptor)
		{
			if (_descriptor < 0)
			{
				throw std::system_error(errno, std::system_category(), "invalid descriptor");
			}
		}

		raii_descriptor(const std::filesystem::path& path, int mode = O_RDONLY)
		{
			open(path, mode);
		}

		inline virtual ~raii_descriptor()
		{
			if (_descriptor > 0)
			{
				close(_descriptor);
			}
		}

		void open(const std::filesystem::path& path, int mode)
		{
			if (_descriptor > 0)
			{
				close(_descriptor);
			}

			_descriptor = ::open(path.c_str(), mode);

			if (_descriptor < 0)
			{
				throw std::system_error(errno, std::system_category(), path.c_str());
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

		void write(const void* data, size_t size) const
		{
			int result = ::write(_descriptor, data, size);

			if (result < 0)
			{
				throw std::system_error(errno, std::system_category(), "write");
			}

			if (result != size)
			{
				throw std::system_error(-EIO, std::system_category(), "write");
			}
		}

		template <typename T>
		void write_value(T& t) const
		{
			return raii_descriptor::write(&t, sizeof(T));
		}

		void write_text(std::string_view text) const
		{
			return raii_descriptor::write(text.data(), text.size());
		}

		void fsync()
		{
			::fsync(_descriptor);
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
		gpio_lvp(uint32_t offset, bool value = true) :
			offset(offset),
			value(value)
		{
		}

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
		gpio_chip(const std::filesystem::path& path) :
			raii_descriptor(path)
		{
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

	class pwm_chip
	{
	public:
		pwm_chip(
			const std::filesystem::path& path, 
			uint8_t line_number,
			float frequency,
			float initial_percent = 0) :
			_path(path)
		{
			std::string line = "pwm" + std::to_string(line_number);

			if (!std::filesystem::exists(path / line))
			{
				raii_descriptor export_file(path / "export", O_WRONLY);
				export_file.write_text(std::to_string(line_number));
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
			}

			_period_file.open(path / line / "period", O_WRONLY);
			_duty_cycle_file.open(path / line / "duty_cycle", O_WRONLY);

			set_frequency(frequency);
			set_duty_percent(initial_percent);

			raii_descriptor enabled(path / line / "enable", O_WRONLY);
			enabled.write_text("1");
		}

		void set_frequency(float frequency)
		{
			if (frequency < 0)
			{
				throw std::invalid_argument("Frequency must be a positive float");
			}

			_period_ns = (1.0f / frequency) * 1000000000.0f;
			_period_file.write_text(std::to_string(_period_ns));
		}

		void set_duty_percent(float percent)
		{
			if (percent < 0.0f || percent > 100.0f)
			{
				throw std::invalid_argument("Percentage must be between 0 and 100");
			}

			if (_period_ns < 0)
			{
				throw std::logic_error("Set frequency first!");
			}

			std::string duty_cycle = std::to_string(int64_t(_period_ns * (percent / 100.0f)));

			_duty_cycle_file.write_text(duty_cycle);
		}

	private:
		int64_t _period_ns = 0;
		raii_descriptor _period_file;
		raii_descriptor _duty_cycle_file;

		const std::filesystem::path _path;
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

		gpio_chip chip("/dev/gpiochip4");

		gpio_line_group input_lines =
			chip.lines(GPIO_V2_LINE_FLAG_INPUT, input_pins);

		gpio_line_group output_lines =
			chip.lines(GPIO_V2_LINE_FLAG_OUTPUT, output_pins);

		//gpio_line_group monitor_lines =
		//	chip.lines(GPIO_V2_LINE_FLAG_EDGE_RISING, monitor_pins);

		// Confusingly enough, the Rasperry Pi PWM 0 is in pwmchip2
		// Fans use 25kHz https://www.mouser.com/pdfDocs/San_Ace_EPWMControlFunction.pdf
		pwm_chip pwm("/sys/class/pwm/pwmchip2", 0, 25000);

		uint64_t t = 0;

		raii_descriptor thermal_zone0("/sys/class/thermal/thermal_zone0/temp");

		std::string temperature(5, 0);

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

			pwm.set_duty_percent(t % 100);

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
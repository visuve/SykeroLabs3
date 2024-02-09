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

	class gpio_descriptor
	{
	public:
		constexpr gpio_descriptor(int descriptor) :
			_descriptor(descriptor)
		{
		}

		inline virtual ~gpio_descriptor()
		{
			if (_descriptor > 0)
			{
				close(_descriptor);
			}
		}

		template<typename... Args>
		int ioctl(uint32_t request, Args... args) const
		{
			int result = ::ioctl(_descriptor, request, args...);

			if (result == -1)
			{
				throw std::system_error(errno, std::system_category(), "ioctl");
			}

			return result;
		}

		operator int() const
		{
			return _descriptor;
		}

	protected:
		int _descriptor = 0;
	};

	struct gpio_data
	{
		uint32_t offset = 0;
		bool value = true;
	};

	template <size_t N>
	class gpio_line_group : private gpio_descriptor
	{
	public:
		constexpr gpio_line_group(int descriptor, const uint32_t(&offsets)[N]) :
			gpio_descriptor(descriptor)
		{
			clone(_offsets, offsets);
		}

		void read(std::array<gpio_data, N>& data)
		{
			std::bitset<64> mask;
			std::bitset<64> bits;

			for (size_t i = 0; i < N; ++i)
			{
				mask[i] = true;
			}

			gpio_v2_line_values values = { 0, mask.to_ullong() };
			ioctl(GPIO_V2_LINE_GET_VALUES_IOCTL, &values);
			bits = values.bits;

			for (size_t i = 0; i < N; ++i)
			{
				data[i].offset = _offsets[i];
				data[i].value = bits[i];
			}
		}

		void write(const std::array<gpio_data, N>& data)
		{
			std::bitset<64> mask;
			std::bitset<64> bits;

			for (size_t i = 0; i < N; ++i)
			{
				mask[i] = true;
				bits[i] = data[i].value;
			}

			gpio_v2_line_values values = { bits.to_ullong(), mask.to_ullong() };
			ioctl(GPIO_V2_LINE_SET_VALUES_IOCTL, &values);
		}

	private:
		uint32_t _offsets[N];
	};

	// https://github.com/torvalds/linux/blob/master/tools/gpio/lsgpio.c
	// https://github.com/torvalds/linux/blob/master/tools/gpio/gpio-utils.c
	class gpio_chip : public gpio_descriptor
	{
	public:
		inline gpio_chip(std::string_view name) :
			gpio_descriptor(open(name.data(), O_RDONLY))
		{
			if (_descriptor < 0)
			{
				throw std::system_error(errno, std::system_category(), name.data());
			}
		}

		template <size_t N>
		gpio_line_group<N> lines(uint64_t flags, const uint32_t(&offsets)[N]) const
		{
			gpio_v2_line_config config = { 0 };
			config.flags = flags;

			gpio_v2_line_request request = { 0 };
			request.config = config;
			request.num_lines = N;

			clone(request.offsets, offsets);
			clone(request.consumer, "sykerolabs");

			gpio_descriptor::ioctl(GPIO_V2_GET_LINE_IOCTL, &request);

			return gpio_line_group<N>(request.fd, offsets);
		}
	};

	int run()
	{
		const uint32_t input_pins[] =
		{
			pins::RESERVOIR_PUMP_1,
			pins::RESERVOIR_PUMP_2
		};

		const uint32_t output_pins[] =
		{
			pins::NFT_PUMP_1,
			pins::NFT_PUMP_2,
			pins::FAN_1_RELAY,
			pins::FAN_2_RELAY
		};

		const uint32_t interrupt_pins[] =
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

		uint64_t t = 0;

		std::ifstream thermal_zone0("/sys/class/thermal/thermal_zone0/temp");

		float temperature = 0.0;

		while (!signaled)
		{
			std::cout << "\nT=" << ++t << ": \n";

			std::array<gpio_data, 2> input_data;
			input_lines.read(input_data);

			for (auto& input : input_data)
			{
				std::cout << input.offset << '=' << input.value << '\n';
			}

			std::array<gpio_data, 4> output_data =
			{
				gpio_data(pins::NFT_PUMP_1, t % 4 == 0),
				gpio_data(pins::NFT_PUMP_2, t % 4 == 1),
				gpio_data(pins::FAN_1_RELAY, t % 4 == 2),
				gpio_data(pins::FAN_2_RELAY, t % 4 == 3)
			};

			output_lines.write(output_data);

			thermal_zone0 >> temperature;
			thermal_zone0.seekg(0, std::ios::beg);

			std::cout << "CPU @ " << temperature / 1000 << "c\n";

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
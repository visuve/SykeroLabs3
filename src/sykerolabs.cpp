// https://github.com/brgl/libgpiod/tree/v1.6.x/bindings/cxx/examples
// https://framagit.org/cpb/example-programs-using-libgpiod/-/tree/master

#include "mega.pch"

namespace sl
{
	struct pins
	{
		enum : uint8_t
		{
			NFT_PUMP_1 = 5,
			NFT_PUMP_2 = 6,
			FAN_PWM_SIGNAL = 12,
			RESERVOIR_PUMP_1 = 13,
			RESERVOIR_PUMP_2 = 16,
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

	bool compare_event_time(
		const gpiod::line_event& lhs, 
		const gpiod::line_event& rhs)
	{
		return lhs.timestamp < rhs.timestamp;
	}

	float rpm(gpiod::line& line)
	{
		line.event_wait(std::chrono::seconds(10));

		// Sigh; this is a blocking function. I need to move this to a thread
		// as gpiod does not appear to have async functions anywhere
		std::vector<gpiod::line_event> events = line.event_read_multiple();

		// https://github.com/brgl/libgpiod/blob/v1.6.x/bindings/cxx/line.cpp#L248
		assert(events.size() == 16); 

		auto older = std::min_element(events.cbegin(), events.cend(), compare_event_time);
		auto newer = std::max_element(events.cbegin(), events.cend(), compare_event_time);

		std::chrono::nanoseconds diff = newer->timestamp - older->timestamp;

		float revolutions_per_nanosecond = 16.0f / diff.count();

		constexpr float nanoseconds_in_minute = 
			std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::minutes(1)).count();

		return revolutions_per_nanosecond * nanoseconds_in_minute;
	}

	int run()
	{
		constexpr char chip_name[] = "gpiochip4";

		gpiod::chip chip(chip_name);

		const std::vector<uint32_t> input_pins =
		{
			pins::NFT_PUMP_1,
			pins::NFT_PUMP_2,
			pins::RESERVOIR_PUMP_1,
			pins::RESERVOIR_PUMP_2
		};

		const std::vector<uint32_t> output_pins =
		{
			pins::FAN_1_RELAY,
			pins::FAN_2_RELAY
		};

		const std::vector<uint32_t> interrupt_pins =
		{
			pins::FAN_1_TACHOMETER,
			pins::FAN_2_TACHOMETER
		};

		const size_t interrupt_pin_count = sizeof(interrupt_pins) / sizeof(uint32_t);

		gpiod::line_bulk inputs = chip.get_lines(input_pins);
		inputs.request({ "sykerolabs_in", gpiod::line_request::DIRECTION_INPUT, 0 });

		gpiod::line_bulk outputs = chip.get_lines(output_pins);
		outputs.request({ "sykerolabs_out", gpiod::line_request::DIRECTION_OUTPUT, 0 });

		gpiod::line_bulk interrupts = chip.get_lines(interrupt_pins);
		interrupts.request({ "sykerolabs_irq", gpiod::line_request::EVENT_RISING_EDGE, 0 });

		uint64_t t = 0;

		while (!signaled)
		{
			std::cout << "\nPIM " << ++t << ": \n";

			inputs.event_wait(std::chrono::seconds(1));

			std::vector<int> values = inputs.get_values();

			assert(values.size() == 4);

			size_t i = static_cast<size_t>(-1);

			std::cout << "NFT pump 1 relay = " << !values[++i] << '\n';
			std::cout << "NFT pump 2 relay = " << !values[++i] << '\n';

			std::cout << "Pool pump 1 relay = " << !values[++i] << '\n';
			std::cout << "Pool pump 2 relay = " << !values[++i] << '\n';

			int fan1_value = t % 10 == 0;
			int fan2_value = t % 10 != 0;

			outputs.set_values({ fan1_value, fan2_value });

			std::cout << "Fan 1 relay = " << fan1_value << '\n';
			std::cout << "Fan 2 relay = " << fan2_value << '\n';

			//std::cout << "Fan 1 RPM = " << rpm(interrupts.get(0)) << '\n';
			//std::cout << "Fan 2 RPM = " << rpm(interrupts.get(1)) << '\n';

			std::this_thread::sleep_for(std::chrono::seconds(1));
		}
	}
}

int main()
{
	std::signal(SIGINT, sl::signal_handler);

	try
	{
		return sl::run();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << std::endl;
	}

	return -1;
}
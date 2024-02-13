#include "mega.pch"
#include "sykero_io.hpp"
#include "sykero_gpio.hpp"
#include "sykero_pwm.hpp"

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
		syslog(LOG_NOTICE, "Signaled: %d", signal);
		signaled = signal;
	}

	constexpr size_t fan_count = 2;
	std::atomic<int64_t> fan_rpms[fan_count] = { 0 };

	void measure_fans(const gpio_line_group& monitor_lines)
	{
		int64_t revolutions[fan_count] = { 0 };
		int64_t measure_start[fan_count] = { 0 };
		gpio_v2_line_event event = {};

		while (!signaled)
		{
			if (!monitor_lines.poll(std::chrono::milliseconds(1000)))
			{
				continue; // Meh, not ready
			}

			while (!signaled && monitor_lines.read_event(event))
			{
				size_t fan_index = event.offset - pins::FAN_1_TACHOMETER;

				if (event.line_seqno % 10 == 1)
				{
					revolutions[fan_index] = 1;
					measure_start[fan_index] = event.timestamp_ns;
					continue;
				}
				
				if (event.line_seqno % 10 == 0)
				{
					double time_taken_ns = event.timestamp_ns - measure_start[fan_index];
					double revolutions_per_nanoseconds = ++revolutions[fan_index] / time_taken_ns;

					constexpr auto nanos_per_minute = 
						std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::minutes(1));

					fan_rpms[fan_index] = nanos_per_minute.count() * revolutions_per_nanoseconds;
					continue;
				}
				
				++revolutions[fan_index];
			}
		}
	}

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
			chip.line_group(GPIO_V2_LINE_FLAG_INPUT, input_pins);

		gpio_line_group output_lines =
			chip.line_group(GPIO_V2_LINE_FLAG_OUTPUT, output_pins);

		gpio_line_group monitor_lines =
			chip.line_group(GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_EDGE_RISING, monitor_pins);

		// Confusingly enough, the Rasperry Pi PWM 0 is in pwmchip2
		// Fans use 25kHz https://www.mouser.com/pdfDocs/San_Ace_EPWMControlFunction.pdf
		// https://noctua.at/pub/media/wysiwyg/Noctua_PWM_specifications_white_paper.pdf
		pwm_chip pwm("/sys/class/pwm/pwmchip2", 0, 25000);

		uint64_t t = 0;

		file_descriptor thermal_zone0("/sys/class/thermal/thermal_zone0/temp");

		std::string temperature(5, 0);

		std::jthread thread(measure_fans, monitor_lines);

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
				std::cout << "GPIO " << input.offset << " = " << input.value << '\n';
			}

			std::array<gpio_lvp, 4> output_data =
			{
				gpio_lvp(pins::NFT_PUMP_1, t % 4 == 0),
				gpio_lvp(pins::NFT_PUMP_2, t % 4 == 1),
				gpio_lvp(pins::FAN_1_RELAY, t % 4 == 2),
				gpio_lvp(pins::FAN_2_RELAY, t % 4 == 3)
			};

			output_lines.write_values(output_data);
			
			for (size_t i = 0; i < fan_count; ++i)
			{
				std::cout << "Fan " << i + 1 << " = " << fan_rpms[i] << " RPM\n";
			}

			thermal_zone0.read_text(temperature);
			thermal_zone0.lseek(0, SEEK_SET);

			std::cout << "CPU = " << std::stof(temperature) / 1000.0f << "c\n";

			pwm.set_duty_percent(t % 100);

			sl::nanosleep(std::chrono::milliseconds(1000));
		}

		return 0;
	}

	struct syslog_facility
	{
		syslog_facility(int facility)
		{
#ifdef NDEBUG
			setlogmask(LOG_UPTO(LOG_WARNING));
#else
			setlogmask(LOG_UPTO(LOG_DEBUG));
#endif
			openlog("sykerolabs", LOG_CONS | LOG_PID | LOG_NDELAY, facility);

			syslog(LOG_INFO, "starting...");
		}

		~syslog_facility()
		{
			syslog(LOG_INFO, "stopped!");

			closelog();
		}
	};
}

int main()
{
	sl::syslog_facility slsf(LOG_LOCAL0);

	std::signal(SIGINT, sl::signal_handler);

	try
	{
		return sl::run();
	}
	catch (const std::system_error& e)
	{
		syslog(LOG_CRIT, "std::system_error: %s", e.what());
	}
	catch (const std::exception& e)
	{
		syslog(LOG_CRIT, "std::exception: %s", e.what());
	}

	return -1;
}
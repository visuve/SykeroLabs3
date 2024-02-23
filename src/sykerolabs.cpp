#include "mega.pch"
#include "sykero_mem.hpp"
#include "sykero_io.hpp"
#include "sykero_gpio.hpp"
#include "sykero_pwm.hpp"

namespace sl
{
	// See https://github.com/visuve/SykeroLabs3/wiki for more details
	namespace pins
	{
		enum : uint8_t
		{
			ENVIRONMENT_TEMPERATURE = 4,
			WATER_LEVEL_SENSOR_1 = 5,
			WATER_LEVEL_SENSOR_2 = 6,
			IRRIGATION_PUMP_1 = 13,
			IRRIGATION_PUMP_2 = 16,
			FAN_SPEED_SIGNAL = 12, // I use one for both
			FAN_1_RELAY = 19,
			FAN_2_RELAY = 20,
			FAN_1_TACHOMETER = 23,
			FAN_2_TACHOMETER = 24
		};
	}

	std::atomic<int> signaled = 0;

	constexpr size_t water_level_sensor_count = 2;
	std::array<std::atomic<bool>, water_level_sensor_count> water_level_sensor_states;

	constexpr size_t fan_count = 2;
	std::array<std::atomic<uint16_t>, fan_count> fan_rpms;

	std::atomic<float> cpu_celcius = 0;
	std::atomic<float> environment_celcius = 0;

	void measure_fans(const gpio_line_group& fans)
	{
		clear(fan_rpms);

		std::array<float, fan_count> revolutions;
		clear(revolutions);

		std::array<float, fan_count> measure_start;
		clear(measure_start);

		gpio_v2_line_event event;
		clear(event);

		while (!signaled)
		{
			while (fans.poll(std::chrono::milliseconds(100)) && fans.read_event(event))
			{
				assert(event.id == GPIO_V2_LINE_EVENT_RISING_EDGE);

				size_t fan_index = event.offset - pins::FAN_1_TACHOMETER;

				assert(fan_index < fan_count);

				if (event.line_seqno % 10 == 1)
				{
					revolutions[fan_index] = 1.0f;
					measure_start[fan_index] = event.timestamp_ns;
					continue;
				}

				if (event.line_seqno % 10 == 0)
				{
					float time_taken_ns = float(event.timestamp_ns) - measure_start[fan_index];
					float revolutions_per_nanoseconds = revolutions[fan_index] / time_taken_ns;

					constexpr auto nanos_per_minute = static_cast<float>(
						std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::minutes(1)).count());

					float rpm = nanos_per_minute * revolutions_per_nanoseconds;

					assert(rpm >= 0 && rpm < 0xFFFF);

					fan_rpms[fan_index] = static_cast<uint16_t>(rpm);

					continue;
				}

				++revolutions[fan_index];
			}

			clear(fan_rpms);
		}
	}

	void monitor_water_level_sensors(const gpio_line_group& water_level_sensors)
	{
		clear(water_level_sensor_states);

		gpio_v2_line_event event;
		clear(event);

		while (!signaled)
		{
			while (water_level_sensors.poll(std::chrono::milliseconds(100)) && water_level_sensors.read_event(event))
			{
				assert(event.id == GPIO_V2_LINE_EVENT_RISING_EDGE || event.id == GPIO_V2_LINE_EVENT_FALLING_EDGE);

				size_t sensor_index = event.offset - pins::WATER_LEVEL_SENSOR_1;

				assert(sensor_index < water_level_sensor_count);

				auto& water_level_sensor_state = water_level_sensor_states[sensor_index];

				water_level_sensor_state = event.id == GPIO_V2_LINE_EVENT_RISING_EDGE ? false : true;

				syslog(LOG_NOTICE,
					"Water level sensor %zu changed to %s",
					++sensor_index,
					event.id == GPIO_V2_LINE_EVENT_RISING_EDGE ? "high" : "low");
			}
		}
	}

	std::filesystem::path find_temperature_sensor_path()
	{
		const std::regex regex("^28-[0-9a-f]{12}$");

		for (const auto& entry : std::filesystem::directory_iterator("/sys/bus/w1/devices/"))
		{
			const std::filesystem::path path = entry.path();
			const std::string filename = path.filename().string();

			if (std::regex_search(filename, regex))
			{
				return path / "temperature";
			}
		}

		throw std::runtime_error("Temperature sensor not found");
	}

	void measure_temperature(const file_descriptor& thermal_zone0, const file_descriptor& ds18b20)
	{
		std::string buffer(5, 0);

		while (!signaled)
		{
			auto before = std::chrono::steady_clock::now();

			thermal_zone0.read_text(buffer);
			thermal_zone0.lseek(0, SEEK_SET);

			cpu_celcius = std::stof(buffer) / 1000.0f;

			ds18b20.read_text(buffer); // Reading a DS18B20 has an intrinsic delay of 750ms
			ds18b20.lseek(0, SEEK_SET);

			environment_celcius = std::stof(buffer) / 1000.0f;

			auto after = std::chrono::steady_clock::now();
			auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(after - before);
			auto delay = diff - std::chrono::milliseconds(1000);

			if (delay > std::chrono::milliseconds(0))
			{
				sl::nanosleep(delay);
			}
		}
	}

	void signal_handler(int signal)
	{
		syslog(LOG_NOTICE, "Signaled: %d", signal);
		signaled = signal;
	}

	template<typename Function, typename... Args>
	int exception_handler(Function&& function, Args&&... args)
	{
		try
		{
			function(args...);
		}
		catch (const std::system_error& e)
		{
			syslog(LOG_CRIT, "std::system_error: %s", e.what());

			return e.code().value();
		}
		catch (const std::exception& e)
		{
			syslog(LOG_CRIT, "std::exception: %s", e.what());

			return -1;
		}

		return 0;
	}

	void run()
	{
		const std::set<uint32_t> water_level_sensor_pins =
		{
			pins::WATER_LEVEL_SENSOR_1,
			pins::WATER_LEVEL_SENSOR_2
		};

		const std::set<uint32_t> output_pins =
		{
			pins::IRRIGATION_PUMP_1,
			pins::IRRIGATION_PUMP_2,
			pins::FAN_1_RELAY,
			pins::FAN_2_RELAY
		};

		const std::set<uint32_t> fan_tachometer_pins =
		{
			pins::FAN_1_TACHOMETER,
			pins::FAN_2_TACHOMETER
		};

		gpio_chip chip("/dev/gpiochip4");

		// I do not have an oscilloscope so these values are arbitrary
		constexpr auto water_level_sensor_debounce = std::chrono::milliseconds(10);
		constexpr auto fan_tacho_debounce = std::chrono::microseconds(100);

		gpio_line_group output_lines =
			chip.line_group(GPIO_V2_LINE_FLAG_OUTPUT, output_pins);

		gpio_line_group water_level_sensors =
			chip.line_group(GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_EDGE_RISING | GPIO_V2_LINE_FLAG_EDGE_FALLING,
				water_level_sensor_pins,
				water_level_sensor_debounce);

		gpio_line_group fans =
			chip.line_group(GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_EDGE_RISING,
				fan_tachometer_pins,
				fan_tacho_debounce);

		// Confusingly enough, the Rasperry Pi PWM 0 is in pwmchip2
		// Fans use 25kHz https://www.mouser.com/pdfDocs/San_Ace_EPWMControlFunction.pdf
		// https://noctua.at/pub/media/wysiwyg/Noctua_PWM_specifications_white_paper.pdf
		pwm_chip fan_pwm("/sys/class/pwm/pwmchip2", 0, 25000);

		file_descriptor thermal_zone0("/sys/class/thermal/thermal_zone0/temp");
		file_descriptor ds18b20(find_temperature_sensor_path());

		uint64_t t = 0;

		std::jthread water_level_monitoring_thread([&]()
		{
			exception_handler(monitor_water_level_sensors, water_level_sensors);
		});

		std::jthread fan_measurement_thread([&]()
		{
			exception_handler(measure_fans, fans);
		});

		std::jthread temperature_measurement_thread([&]()
		{
			exception_handler(measure_temperature, thermal_zone0, ds18b20);
		});

		while (!signaled)
		{
			std::cout << "\nT=" << ++t << ": \n";

			std::array<gpio_lvp, 4> output_data =
			{
				gpio_lvp(pins::IRRIGATION_PUMP_1, t % 4 == 0),
				gpio_lvp(pins::IRRIGATION_PUMP_2, t % 4 == 1),
				gpio_lvp(pins::FAN_1_RELAY, t % 4 == 2),
				gpio_lvp(pins::FAN_2_RELAY, t % 4 == 3)
			};

			output_lines.write_values(output_data);

			for (size_t i = 0; i < water_level_sensor_states.size(); ++i)
			{
				std::cout << "Water level sensor " << i + 1 << " = " << (water_level_sensor_states[i] ? "Low" : "High") << '\n';
			}

			for (size_t i = 0; i < fan_rpms.size(); ++i)
			{
				std::cout << "Fan " << i + 1 << " = " << fan_rpms[i] << " RPM\n";
			}

			std::cout << "CPU = " << cpu_celcius << "c\n";
			std::cout << "Environment = " << environment_celcius << "c\n";

			fan_pwm.set_duty_percent(t % 100);

			sl::nanosleep(std::chrono::milliseconds(1000));
		}
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

	return sl::exception_handler(sl::run);
}
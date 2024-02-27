#include "mega.pch"
#include "sykero_mem.hpp"
#include "sykero_io.hpp"
#include "sykero_gpio.hpp"
#include "sykero_pwm.hpp"
#include "sykero_log.hpp"
#include "sykero_csv.hpp"
#include "sykero_time.hpp"

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

	void measure_fans(const gpio::line_group& fans)
	{
		mem::clear(fan_rpms);

		std::array<float, fan_count> revolutions;
		mem::clear(revolutions);

		std::array<float, fan_count> measure_start;
		mem::clear(measure_start);

		gpio_v2_line_event event;
		mem::clear(event);

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

			mem::clear(fan_rpms);
		}
	}

	void monitor_water_level_sensors(const gpio::line_group& water_level_sensors)
	{
		{
			std::array<gpio::line_value_pair, water_level_sensor_count> data =
			{
				gpio::line_value_pair(pins::WATER_LEVEL_SENSOR_1),
				gpio::line_value_pair(pins::WATER_LEVEL_SENSOR_2)
			};

			water_level_sensors.read_values(data);

			water_level_sensor_states[0] = data[0].value;
			water_level_sensor_states[1] = data[1].value;
		}

		gpio_v2_line_event event;
		mem::clear(event);

		while (!signaled)
		{
			while (water_level_sensors.poll(std::chrono::milliseconds(100)) && water_level_sensors.read_event(event))
			{
				assert(event.id == GPIO_V2_LINE_EVENT_RISING_EDGE || event.id == GPIO_V2_LINE_EVENT_FALLING_EDGE);

				size_t sensor_index = event.offset - pins::WATER_LEVEL_SENSOR_1;

				assert(sensor_index < water_level_sensor_count);

				auto& water_level_sensor_state = water_level_sensor_states[sensor_index];

				water_level_sensor_state = event.id == GPIO_V2_LINE_EVENT_RISING_EDGE ? true : false;

				log_notice("Water level sensor %zu changed to %s",
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

	void measure_temperature(const io::file_descriptor& thermal_zone0, const io::file_descriptor& ds18b20)
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
				time::nanosleep(delay);
			}
		}
	}

	void signal_handler(int signal)
	{
		log_notice("Signaled: %d", signal);
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
			log_critical("std::system_error: %s", e.what());

			return e.code().value();
		}
		catch (const std::exception& e)
		{
			log_critical("std::exception: %s", e.what());

			return -1;
		}

		return 0;
	}

	void run()
	{
		csv::file<12u> csv("/dev/stdout",
		{
				"Time",
				"Water Level Sensor 1",
				"Water Level Sensor 2",
				"Pump 1 Relay",
				"Pump 2 Relay",
				"Environment Temperature",
				"Fan 1 Relay",
				"Fan 2 Relay",
				"Fan Duty Percent",
				"Fan 1 RPM",
				"Fan 2 RPM",
				"CPU Temperature" 
		});

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

		gpio::chip chip("/dev/gpiochip4");

		// I do not have an oscilloscope so these values are arbitrary
		constexpr auto water_level_sensor_debounce = std::chrono::milliseconds(10);
		constexpr auto fan_tacho_debounce = std::chrono::microseconds(100);

		gpio::line_group output_lines =
			chip.line_group(GPIO_V2_LINE_FLAG_OUTPUT, output_pins);

		gpio::line_group water_level_sensors =
			chip.line_group(GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_EDGE_RISING | GPIO_V2_LINE_FLAG_EDGE_FALLING,
				water_level_sensor_pins,
				water_level_sensor_debounce);

		gpio::line_group fans =
			chip.line_group(GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_EDGE_RISING,
				fan_tachometer_pins,
				fan_tacho_debounce);

		// Confusingly enough, the Rasperry Pi PWM 0 is in pwmchip2
		// Fans use 25kHz https://www.mouser.com/pdfDocs/San_Ace_EPWMControlFunction.pdf
		// https://noctua.at/pub/media/wysiwyg/Noctua_PWM_specifications_white_paper.pdf
		pwm::chip fan_pwm("/sys/class/pwm/pwmchip2", 0, 25000);

		io::file_descriptor thermal_zone0("/sys/class/thermal/thermal_zone0/temp");
		io::file_descriptor ds18b20(find_temperature_sensor_path());

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
			std::array<gpio::line_value_pair, 4> output_data =
			{
				gpio::line_value_pair(pins::IRRIGATION_PUMP_1, t % 4 == 0),
				gpio::line_value_pair(pins::IRRIGATION_PUMP_2, t % 4 == 1),
				gpio::line_value_pair(pins::FAN_1_RELAY, t % 4 == 2),
				gpio::line_value_pair(pins::FAN_2_RELAY, t % 4 == 3)
			};

			output_lines.write_values(output_data);

			float duty_percent = t % 100;

			fan_pwm.set_duty_percent(duty_percent);

			csv.append_row(
				++t,
				water_level_sensor_states[0].load() ? "High" : "Low",
				water_level_sensor_states[1].load() ? "High" : "Low",
				"Off",
				"Off",
				environment_celcius.load(),
				"On",
				"On",
				duty_percent,
				fan_rpms[0].load(),
				fan_rpms[1].load(),
				cpu_celcius.load());

			time::nanosleep(std::chrono::milliseconds(1000));
		}
	}
}

int main()
{
	sl::log::facility log_facility(1 << 3);

	std::signal(SIGINT, sl::signal_handler);

	return sl::exception_handler(sl::run);
}
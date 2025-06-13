#include "mega.pch"
#include "sykerolabs.hpp"
#include "sykero_mem.hpp"
#include "sykero_io.hpp"
#include "sykero_gpio.hpp"
#include "sykero_pwm.hpp"
#include "sykero_log.hpp"
#include "sykero_csv.hpp"
#include "sykero_time.hpp"

namespace sl
{
	std::stop_source common_stop_source;
	std::array<bool, PUMP_COUNT> pump_states;
	std::array<std::atomic<bool>, WATER_LEVEL_SENSOR_COUNT> water_level_sensor_states;
	std::array<std::atomic<uint16_t>, FAN_COUNT> fan_rpms;
	float duty_percent = DUTY_PERCENTAGE_MIN;

	void monitor_water_level_sensors(std::stop_source stop_source, const gpio::line_group& water_level_sensors)
	{
		log_debug("thread %d monitor_water_level_sensors started.", gettid());

		try
		{
			std::stop_token stop_token = stop_source.get_token();

			{
				std::array<gpio::line_value_pair, WATER_LEVEL_SENSOR_COUNT> data =
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

			while (!stop_token.stop_requested())
			{
				while (water_level_sensors.poll(std::chrono::milliseconds(100)) && water_level_sensors.read_event(event))
				{
					assert(event.id == GPIO_V2_LINE_EVENT_RISING_EDGE || event.id == GPIO_V2_LINE_EVENT_FALLING_EDGE);

					size_t sensor_index = event.offset - pins::WATER_LEVEL_SENSOR_1;

					assert(sensor_index < WATER_LEVEL_SENSOR_COUNT);

					auto& water_level_sensor_state = water_level_sensor_states[sensor_index];

					water_level_sensor_state = event.id == GPIO_V2_LINE_EVENT_RISING_EDGE ? true : false;

					log_notice("water level sensor %zu changed to %s.",
						++sensor_index,
						event.id == GPIO_V2_LINE_EVENT_RISING_EDGE ? "high" : "low");
				}

				std::this_thread::yield();
			}
		}
		catch (const std::system_error& e)
		{
			log_critical("std::system_error: %s", e.what());
		}
		catch (const std::exception& e)
		{
			log_critical("std::exception: %s", e.what());
		}

		log_debug("thread %d monitor_water_level_sensors stopped.", gettid());
	}

	void measure_fans(std::stop_source stop_source, const gpio::line_group& fans)
	{
		log_debug("thread %d measure_fans started.", gettid());

		try
		{
			std::stop_token stop_token = stop_source.get_token();

			mem::clear(fan_rpms);

			std::array<float, FAN_COUNT> revolutions;
			mem::clear(revolutions);

			std::array<float, FAN_COUNT> measure_start;
			mem::clear(measure_start);

			gpio_v2_line_event event;
			mem::clear(event);

			while (!stop_token.stop_requested())
			{
				while (fans.poll(std::chrono::milliseconds(100)) && fans.read_event(event))
				{
					assert(event.id == GPIO_V2_LINE_EVENT_RISING_EDGE);

					size_t fan_index = event.offset - pins::FAN_1_TACHOMETER;

					assert(fan_index < FAN_COUNT);

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
				std::this_thread::yield();
			}
		}
		catch (const std::system_error& e)
		{
			log_critical("std::system_error: %s", e.what());
		}
		catch (const std::exception& e)
		{
			log_critical("std::exception: %s", e.what());
		}

		log_debug("thread %d measure_fans stopped.", gettid());
	}

	void toggle_irrigation(const gpio::line_group& irrigation_pumps, int minute)
	{
		const bool pump1 = minute % 10 == 0;
		const bool pump2 = minute % 10 == 5;

		std::array<gpio::line_value_pair, 2> states =
		{
			gpio::line_value_pair(pins::PUMP_1_RELAY, !pump1),
			gpio::line_value_pair(pins::PUMP_2_RELAY, !pump2),
		};

		irrigation_pumps.write_values(states);
		pump_states = { pump1, pump2 };
	}

	void adjust_fans(const gpio::line_group& fan_relay, pwm::chip& pwm, float temperature)
	{
		gpio::line_value_pair state(pins::FAN_RELAY, !(temperature > MIN_FAN_TOGGLE_CELCIUS));

		if (temperature <= MIN_FAN_TOGGLE_CELCIUS)
		{
			duty_percent = DUTY_PERCENTAGE_MIN;

		}
		else if (temperature >= MAX_FAN_TOGGLE_CELCIUS)
		{
			duty_percent = DUTY_PERCENTAGE_MAX;
		}
		else
		{
			duty_percent = (temperature - MIN_FAN_TOGGLE_CELCIUS) * FAN_TEMPERATURE_STEP;
		}

		pwm.set_duty_percent(duty_percent);
		fan_relay.write_value(state);
	}

	void signal_handler(int signal)
	{
		log_notice("signaled: %d.", signal);
		common_stop_source.request_stop();
	}

	std::filesystem::path csv_file_timestamped_path()
	{
#ifndef NDEBUG
		if (isatty(STDOUT_FILENO) == 1)
		{
			return "/dev/stdout";
		}
#endif
		const std::filesystem::path home(getenv("HOME"));

		const auto sykerolabs = home / "sykerolabs";

		if (!std::filesystem::exists(sykerolabs))
		{
			std::filesystem::create_directory(sykerolabs);
		}

		return sykerolabs / (time::date_string() + ".csv");
	}

	std::filesystem::path find_iio_device(const std::string_view expected_name)
	{
		std::string name_buffer(0x80, '\0');

		for (size_t i = 0; i < MAX_IIO_DEVICES; ++i)
		{
			const std::filesystem::path device_name_path = paths::IIO_DEVICE.string() + std::to_string(i) + "/name";

			if (!std::filesystem::exists(device_name_path))
			{
				log_warning("%s does not exist, skipping.", device_name_path.c_str());
				continue;
			}

			io::file_descriptor device_name_file(device_name_path);

			size_t bytes_read = device_name_file.read_text(name_buffer);

			if (bytes_read && name_buffer.substr(0, bytes_read - 1) == expected_name)
			{
				return device_name_path.parent_path();
			}
		}

		const std::string error_message = 
			"Device " + std::string(expected_name) + " not found in /sys/bus/iio/devices/iio:device*";

		throw std::runtime_error(error_message);
	}

	void run()
	{
		csv::file<15u> csv(csv_file_timestamped_path(),
		{
			"Time",
			"CPU Temperature",
			"Air Temperature",
			"Air Humidity",
			"Air Pressure",
			"Water Level Sensor 1",
			"Water Level Sensor 2",
			"Pump 1 Relay",
			"Pump 2 Relay",
			"Fan Relay",
			"Fan Duty Percent",
			"Fan 1 Speed",
			"Fan 2 Speed",
			"Pool 1 EC",
			"Pool 2 EC"
		});

		const auto rotate_csv = [&]()
		{
			csv.initialize(csv_file_timestamped_path());
		};

		const std::chrono::hh_mm_ss first_start = time::time_to_midnight();
		{
			const std::string ttm = time::time_string(first_start);
			log_debug("time to midnight: %s.", ttm.c_str());
		}

		constexpr std::chrono::days interval(1);
		time::timer csv_rotate_timer(rotate_csv, first_start, interval);

		const std::set<uint32_t> water_level_sensor_pins =
		{
			pins::WATER_LEVEL_SENSOR_1,
			pins::WATER_LEVEL_SENSOR_2
		};

		const std::set<uint32_t> irrigation_pump_pins =
		{
			pins::PUMP_1_RELAY,
			pins::PUMP_2_RELAY
		};

		const std::set<uint32_t> fan_relay_pin =
		{
			pins::FAN_RELAY
		};

		const std::set<uint32_t> fan_tachometer_pins =
		{
			pins::FAN_1_TACHOMETER,
			pins::FAN_2_TACHOMETER
		};

		gpio::chip chip(sl::paths::GPIO_CHIP);

		gpio::line_group irrigation_pumps =
			chip.line_group(GPIO_V2_LINE_FLAG_OUTPUT, irrigation_pump_pins);

		gpio::line_group water_level_sensors =
			chip.line_group(GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_EDGE_RISING | GPIO_V2_LINE_FLAG_EDGE_FALLING,
				water_level_sensor_pins,
				WATER_LEVEL_SENSOR_DEBOUNCE);

		gpio::line_group fan_relay =
			chip.line_group(GPIO_V2_LINE_FLAG_OUTPUT, fan_relay_pin);

		gpio::line_group fan_tachometers =
			chip.line_group(GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_EDGE_RISING | GPIO_V2_LINE_FLAG_BIAS_PULL_UP,
				fan_tachometer_pins,
				FAN_TACHOMETER_DEBOUNCE);

		pwm::chip fan_pwm(sl::paths::PWM_CHIP, 0, FAN_PWM_CONTROL_FREQUENCY);

		// Turn off relays on start
		adjust_fans(fan_relay, fan_pwm, ABSOLUTE_ZERO);
		toggle_irrigation(irrigation_pumps, INVALID_MINUTE);

		const std::filesystem::path bme680_path = find_iio_device("bme680");
		const std::filesystem::path ads1115_path = find_iio_device("ads1015"); // ADS1015 and ADS1115 use the same driver

		io::file_descriptor cpu_temp_file(sl::paths::CPU_TEMPERATURE);
		io::file_descriptor air_temp_file(bme680_path / "in_temp_input");
		io::file_descriptor air_humidity_file(bme680_path / "in_humidityrelative_input");
		io::file_descriptor air_pressure_file(bme680_path / "in_pressure_input");
		io::file_descriptor pool1_ec_file(ads1115_path / "in_voltage0_raw");
		io::file_descriptor pool2_ec_file(ads1115_path / "in_voltage1_raw");

		std::jthread water_level_monitoring_thread(monitor_water_level_sensors, common_stop_source, std::cref(water_level_sensors));
		std::jthread fan_measurement_thread(measure_fans, common_stop_source, std::cref(fan_tachometers));

		std::stop_token stop_token = common_stop_source.get_token();

		log_debug("main loop %d started.", gettid());

		// This is basically a 10 min average; I do not want the fans to toggle on and off possibly every minute
		average<float, 10> air_temperature;

		for (int minute = time::local_time().tm_min + 1; !stop_token.stop_requested() && time::sleep_until_next_even<std::chrono::minutes>(); ++minute)
		{
			const auto cpu_temperature = io::value_from_file<float>(cpu_temp_file) / 1000.0f; // celcius
			air_temperature = io::value_from_file<float>(air_temp_file) / 1000.0f;  // celcius
			const auto air_humidity = io::value_from_file<float>(air_humidity_file); // relative percent
			const auto air_pressure = io::value_from_file<float>(air_pressure_file); // hectopascal

			const auto pool1_ec = io::value_from_file<int>(pool1_ec_file) / 10; // microsiemens per centimeter
			const auto pool2_ec = io::value_from_file<int>(pool2_ec_file) / 10; // microsiemens per centimeter

			// TODO: reduce unnecessary IO by storing the previous state or something
			if (time::is_night())
			{
				toggle_irrigation(irrigation_pumps, INVALID_MINUTE);
				adjust_fans(fan_relay, fan_pwm, ABSOLUTE_ZERO);
			}
			else
			{
				toggle_irrigation(irrigation_pumps, minute);
				adjust_fans(fan_relay, fan_pwm, air_temperature);
			}

			csv.append_row(
				time::datetime_string(),
				cpu_temperature,
				air_temperature.value(),
				air_humidity,
				air_pressure,
				water_level_sensor_states[0].load() ? STR_HIGH : STR_LOW,
				water_level_sensor_states[1].load() ? STR_HIGH : STR_LOW,
				pump_states[0] ? STR_ON : STR_OFF,
				pump_states[1] ? STR_ON : STR_OFF,
				duty_percent > DUTY_PERCENTAGE_MIN ? STR_ON : STR_OFF,
				duty_percent,
				fan_rpms[0].load(),
				fan_rpms[1].load(),
				pool1_ec,
				pool2_ec);
		}

		// Turn off relays on exit
		adjust_fans(fan_relay, fan_pwm, ABSOLUTE_ZERO);
		toggle_irrigation(irrigation_pumps, INVALID_MINUTE);

		// If the fans are spinning, there should not be a reason to wait for the fans to spool down,
		// because the fan rpm read function will block the jthread from exiting

		log_debug("main loop %d stopped.", gettid());
	}
}

int main(int, char** argv)
{
	std::signal(SIGINT, sl::signal_handler);
	std::signal(SIGTERM, sl::signal_handler);

	sl::log::facility log_facility(1 << 3, argv[0]);

	try
	{
		sl::run();
	}
	catch (const std::system_error& e)
	{
		log_critical("std::system_error: %s.", e.what());

		return e.code().value();
	}
	catch (const std::exception& e)
	{
		log_critical("std::exception: %s.", e.what());

		return -1;
	}

	return 0;
}
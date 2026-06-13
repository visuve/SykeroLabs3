#include "mega.pch"
#include "sykerolabs.hpp"
#include "sykero_mem.hpp"
#include "sykero_io.hpp"
#include "sykero_gpio.hpp"
#include "sykero_pwm.hpp"
#include "sykero_log.hpp"
#include "sykero_csv.hpp"
#include "sykero_time.hpp"
#include "sykero_mppt.hpp"

namespace sl
{
	std::stop_source common_stop_source;

	struct pump_properties
	{
		bool pump1 = false;
		bool pump2 = false;
	};

	struct float_switch_properties
	{
		bool sensor1 = false;
		bool sensor2 = false;

		void save(uint32_t offset, uint32_t identifier)
		{
			uint32_t index = offset - pins::WATER_LEVEL_SENSOR_1;
			bool state = identifier == GPIO_V2_LINE_EVENT_RISING_EDGE ? true : false;

			switch (index)
			{
			case 0:
				sensor1 = state;
				break;
			case 1:
				sensor2 = state;
				break;
			default:
				log_error("invalid identifier %u", index);
				return;
			}

			log_notice("float switch %zu changed to %s.", ++index, state ? "high" : "low");
		}
	};

	struct fan_properties
	{
		uint32_t fan1_rpm = 0;
		uint32_t fan2_rpm = 0;

		void save(uint32_t index, uint32_t rpm)
		{
			switch (index)
			{
			case 0:
				fan1_rpm = rpm;
				break;
			case 1:
				fan2_rpm = rpm;
				break;
			default:
				log_error("invalid fan index %zu", index);
				return;
			}
		}
	};

	struct tds_properties
	{
		snapshot<uint32_t, std::deci> pool1;
		snapshot<uint32_t, std::deci> pool2;
	};

	property_group<pump_properties> pump_data;
	property_group<float_switch_properties> float_switch_data;
	property_group<fan_properties> fan_data;
	property_group<tds_properties> tds_data;

	void monitor_float_switches(std::stop_source stop_source, const gpio::line_group& float_switches)
	{
		log_debug("thread %d monitor_float_switches started.", gettid());

		try
		{
			std::stop_token stop_token = stop_source.get_token();

			{
				std::array<gpio::line_value_pair, 2> data =
				{
					gpio::line_value_pair(pins::WATER_LEVEL_SENSOR_1),
					gpio::line_value_pair(pins::WATER_LEVEL_SENSOR_2)
				};

				float_switches.read_values(data);

				auto fsd = float_switch_data.acquire();
				fsd->sensor1 = data[0].value;
				fsd->sensor2 = data[1].value;
			}

			gpio_v2_line_event event;
			mem::clear(event);

			while (!stop_token.stop_requested())
			{
				while (float_switches.poll(std::chrono::milliseconds(100)) && float_switches.read_event(event))
				{
					auto fsd = float_switch_data.acquire();
					fsd->save(event.offset, event.id);
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

		log_debug("thread %d monitor_float_switches stopped.", gettid());
	}

	void measure_fans(std::stop_source stop_source, const gpio::line_group& fan_tachometers)
	{
		log_debug("thread %d measure_fans started.", gettid());

		try
		{
			std::stop_token stop_token = stop_source.get_token();

			gpio_v2_line_event event;
			mem::clear(event);

			frequency_counter<float, std::chrono::minutes> fan_speeds[2];

			while (!stop_token.stop_requested())
			{
				while (fan_tachometers.poll(std::chrono::milliseconds(100)) && fan_tachometers.read_event(event))
				{
					const auto time = std::chrono::nanoseconds(event.timestamp_ns);
					const uint32_t fan_index = event.offset - pins::FAN_1_TACHOMETER;
					auto& fan_speed = fan_speeds[fan_index];

					if (event.line_seqno % 10 != 0)
					{
						fan_speed.update(time);
					}
					else
					{
						const float rpm = fan_speed.get(time);
						fan_speed.reset();

						auto fd = fan_data.acquire();
						fd->save(event.offset, static_cast<uint32_t>(rpm));
					}
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

		log_debug("thread %d measure_fans stopped.", gettid());
	}

	void measure_tds(std::stop_source stop_source, const gpio::line_group& tds_probe_relay, io::file_descriptor& pool1_ec_file, io::file_descriptor& pool2_ec_file)
	{
		log_debug("thread %d measure_tds started.", gettid());

		constexpr gpio::line_value_pair PROBES_ON(pins::TDS_PROBE_RELAY, false);
		constexpr gpio::line_value_pair PROBES_OFF(pins::TDS_PROBE_RELAY, true);

		try
		{
			std::stop_token stop_token = stop_source.get_token();

			do
			{
				tds_probe_relay.write_value(PROBES_ON);

				// The sampling interval is 8 times in a second, see datarate parameters in
				// https://github.com/visuve/SykeroLabs3/wiki/Operating-system-configuration#full-bootfirmwareconfigtxt
				std::this_thread::sleep_for(TDS_PROBE_WAKEUP_DELAY);

				{
					auto tds = tds_data.acquire();
					tds->pool1.parse(io::peek_some(pool1_ec_file)).commit();
					tds->pool2.parse(io::peek_some(pool2_ec_file)).commit();
				}

				tds_probe_relay.write_value(PROBES_OFF);

				// Sleep/wait for the next measurement
				for (std::chrono::seconds i(0); i < TDS_READ_INTERVAL && !stop_token.stop_requested(); ++i)
				{
					std::this_thread::yield();
					std::this_thread::sleep_for(std::chrono::seconds(1));
				}

			} while (!stop_token.stop_requested());

		}
		catch (const std::system_error& e)
		{
			log_critical("std::system_error: %s", e.what());
		}
		catch (const std::exception& e)
		{
			log_critical("std::exception: %s", e.what());
		}

		log_debug("thread %d measure_tds stopped.", gettid());
	}

	void monitor_mppt(std::stop_source stop_source, mppt::controller& mppt)
	{
		log_debug("thread %d monitor_mppt started.", gettid());

		try
		{
			std::stop_token stop_token = stop_source.get_token();
			std::array<uint8_t, MAX_SERIAL_BUFFER_SIZE> buffer;
			std::chrono::steady_clock::time_point last_valid_block = std::chrono::steady_clock::now();
			std::chrono::steady_clock::time_point last_warning = last_valid_block - std::chrono::minutes(1);

			while (!stop_token.stop_requested())
			{
				auto data = mppt.read_serial(buffer);

				if (data.empty())
				{
					std::this_thread::yield();
					continue;
				}

				// I do not care about millisecond precision here, so this can be called before parsing the block
				const auto now = std::chrono::steady_clock::now();

				if (mppt.parse(data))
				{
					last_valid_block = now;
				}

				const auto since_valid = now - last_valid_block;
				const auto since_warning = now - last_warning;

				if (since_valid >= std::chrono::minutes(1) && since_warning >= std::chrono::minutes(1))
				{
					const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(since_valid);
					log_warning("no valid block received since %lld minutes", minutes.count());
					last_warning = now;
				}
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

		log_debug("thread %d monitor_mppt stopped.", gettid());
	}

	void toggle_irrigation(const gpio::line_group& irrigation_pumps, int minute)
	{
		const bool pump1 = minute % 10 == 0;
		const bool pump2 = minute % 10 == 5;

		const std::array<gpio::line_value_pair, 2> states =
		{
			gpio::line_value_pair(pins::PUMP_1_RELAY, !pump1),
			gpio::line_value_pair(pins::PUMP_2_RELAY, !pump2),
		};

		irrigation_pumps.write_values(states);

		auto pumps = pump_data.acquire();
		pumps->pump1 = pump1;
		pumps->pump2 = pump2;
	}

	float adjust_fans(const gpio::line_group& fan_relay, pwm::chip& pwm, float temperature)
	{
		float duty_percent = DUTY_PERCENTAGE_INVALID;

		const gpio::line_value_pair state(pins::FAN_RELAY, !(temperature > MIN_FAN_TOGGLE_CELCIUS));

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

		return duty_percent;
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
			std::format("Device {} not found in /sys/bus/iio/devices/iio:device*", expected_name);

		throw std::runtime_error(error_message);
	}

	void run()
	{
		csv::file<24u> csv(csv_file_timestamped_path(),
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
			"Pool 2 EC",
			"Battery Voltage",
			"Battery Current",
			"Panel Voltage",
			"Panel Power",
			"MPPT Load",
			"MPPT State",
			"MPPT Error",
			"MPPT Yield",
			"MPPT Daily Best"
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

		time::timer csv_rotate_timer(rotate_csv, first_start, LOG_ROTATION_INTERVAL);

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

		const std::set<uint32_t> tds_probe_relay_pin =
		{
			pins::TDS_PROBE_RELAY
		};

		const std::set<uint32_t> fan_tachometer_pins =
		{
			pins::FAN_1_TACHOMETER,
			pins::FAN_2_TACHOMETER
		};

		gpio::chip chip(sl::paths::GPIO_CHIP);

		gpio::line_group irrigation_pumps =
			chip.line_group(GPIO_V2_LINE_FLAG_OUTPUT, irrigation_pump_pins);

		gpio::line_group float_switches =
			chip.line_group(GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_EDGE_RISING | GPIO_V2_LINE_FLAG_EDGE_FALLING,
				water_level_sensor_pins,
				WATER_LEVEL_SENSOR_DEBOUNCE);

		gpio::line_group fan_relay =
			chip.line_group(GPIO_V2_LINE_FLAG_OUTPUT, fan_relay_pin);

		gpio::line_group tds_probe_relay =
			chip.line_group(GPIO_V2_LINE_FLAG_OUTPUT, tds_probe_relay_pin);

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
		mppt::controller mppt(sl::paths::SERIAL0);

		std::jthread float_switch_monitoring_thread(monitor_float_switches, common_stop_source, std::cref(float_switches));
		std::jthread fan_measurement_thread(measure_fans, common_stop_source, std::cref(fan_tachometers));
		std::jthread tds_measurement_thread(measure_tds, common_stop_source, std::cref(tds_probe_relay), std::ref(pool1_ec_file), std::ref(pool2_ec_file));
		std::jthread mppt_monitoring_thread(monitor_mppt, common_stop_source, std::ref(mppt));

		std::stop_token stop_token = common_stop_source.get_token();

		log_debug("main loop %d started.", gettid());

		// This is basically a 10 min average; I do not want the fans to toggle on and off possibly every minute
		rolling_average<10, float, std::milli> air_temperature;
		snapshot<float, std::milli> cpu_temperature;
		snapshot<float> air_humidity; // relative percent
		snapshot<float> air_pressure; // hectopascal

		float duty_percent = DUTY_PERCENTAGE_INVALID;

		for (int minute = time::local_time().tm_min + 1; !stop_token.stop_requested() && time::sleep_until_next_even<std::chrono::minutes>(); ++minute)
		{
			cpu_temperature.parse(io::peek_some(cpu_temp_file)).commit();
			air_temperature.parse(io::peek_some(air_temp_file)).commit();
			air_humidity.parse(io::peek_some(air_humidity_file)).commit();
			air_pressure.parse(io::peek_some(air_pressure_file)).commit();

			// TODO: reduce unnecessary IO by storing the previous state or something
			if (time::is_night())
			{
				toggle_irrigation(irrigation_pumps, INVALID_MINUTE);
				duty_percent = adjust_fans(fan_relay, fan_pwm, ABSOLUTE_ZERO);
			}
			else
			{
				toggle_irrigation(irrigation_pumps, minute);
				duty_percent = adjust_fans(fan_relay, fan_pwm, air_temperature.get());
			}

			{
				auto fsd = float_switch_data.acquire();
				auto pd = pump_data.acquire();
				auto fd = fan_data.acquire();
				auto td = tds_data.acquire();
				auto md = mppt.mppt_data.acquire();

				csv.append_row(
					time::datetime_string(),
					cpu_temperature.get(),
					air_temperature.get(),
					air_humidity.get(),
					air_pressure.get(),
					fsd->sensor1 ? STR_HIGH : STR_LOW,
					fsd->sensor2 ? STR_HIGH : STR_LOW,
					pd->pump1 ? STR_ON : STR_OFF,
					pd->pump2 ? STR_ON : STR_OFF,
					duty_percent > DUTY_PERCENTAGE_MIN ? STR_ON : STR_OFF,
					duty_percent,
					fd->fan1_rpm,
					fd->fan2_rpm,
					td->pool1.get(),
					td->pool2.get(),
					md->battery_voltage.get(),
					md->battery_current.get(),
					md->panel_voltage.get(),
					md->panel_power.get(),
					md->load_current.get(),
					md->state.get(),
					md->error.get(),
					md->yield_total.get(),
					md->max_power_today.get());
			}
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
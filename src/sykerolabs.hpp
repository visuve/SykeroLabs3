#pragma once

namespace sl
{
	// See https://github.com/visuve/SykeroLabs3/wiki/Pin-configuration for more details
	namespace pins
	{
		enum : uint8_t
		{
			WATER_LEVEL_SENSOR_1 = 5,
			WATER_LEVEL_SENSOR_2 = 6,
			PUMP_1_RELAY = 13,
			PUMP_2_RELAY = 16,
			FAN_RELAY = 19,
			FAN_1_TACHOMETER = 22,
			FAN_2_TACHOMETER = 23
		};
	}

	namespace paths
	{
		const std::filesystem::path CPU_TEMPERATURE("/sys/class/thermal/thermal_zone0/temp");
		const std::filesystem::path IIO_DEVICE("/sys/bus/iio/devices/iio:device");
#ifdef SYKEROLABS_RPI5
		const std::filesystem::path PWM_CHIP("/sys/class/pwm/pwmchip2");
		const std::filesystem::path GPIO_CHIP("/dev/gpiochip4");
#endif
#ifdef SYKEROLABS_RPIZ2W
		const std::filesystem::path PWM_CHIP("/sys/class/pwm/pwmchip0");
		const std::filesystem::path GPIO_CHIP("/dev/gpiochip0");
#endif
	}

	constexpr float ABSOLUTE_ZERO = -273.15f;
	constexpr float MIN_FAN_TOGGLE_CELCIUS = 20.0f;
	constexpr float MAX_FAN_TOGGLE_CELCIUS = 40.0f;
	constexpr float FAN_TEMPERATURE_STEP = 100.0f / (MAX_FAN_TOGGLE_CELCIUS - MIN_FAN_TOGGLE_CELCIUS);

	constexpr int INVALID_MINUTE = -1;

	constexpr size_t PUMP_COUNT = 2;
	constexpr size_t WATER_LEVEL_SENSOR_COUNT = 2;
	constexpr size_t FAN_COUNT = 2;

	constexpr float DUTY_PERCENTAGE_MIN = 0.0f;
	constexpr float DUTY_PERCENTAGE_MAX = 100.0f;

	// Fans use 25kHz https://www.mouser.com/pdfDocs/San_Ace_EPWMControlFunction.pdf
	// https://noctua.at/pub/media/wysiwyg/Noctua_PWM_specifications_white_paper.pdf
	constexpr float FAN_PWM_CONTROL_FREQUENCY = 25000.0f;

	// I do not have an oscilloscope so these values are arbitrary
	constexpr std::chrono::milliseconds WATER_LEVEL_SENSOR_DEBOUNCE(10);
	constexpr std::chrono::microseconds FAN_TACHOMETER_DEBOUNCE(100);

	// Completely arbitrary value. Change if needed. I have two.
	constexpr size_t MAX_IIO_DEVICES = 9;

	constexpr char STR_ON[] = "on";
	constexpr char STR_OFF[] = "off";
	constexpr char STR_HIGH[] = "high";
	constexpr char STR_LOW[] = "low";

	template<typename T>
	concept Arithmetic = std::is_arithmetic_v<T>;

	template<Arithmetic T, size_t N>
	class average
	{
	public:
		static_assert(N > 0, "N must be greater than zero");

		T value() const
		{
			if (!_count)
			{
				throw std::logic_error("cannot calculate the average of zero samples");
			}

			T sum = T(0);

			for (size_t i = 0; i < _count; ++i)
			{
				sum += _samples[i];
			}

			return sum / static_cast<T>(_count);
		}

		operator T() const
		{
			return value();
		}

		void operator = (T value)
		{
			_samples[_index] = value;
			_count = std::max(_index + 1, _count);
			_index = (_index + 1) % N;
		}

	private:
		T _samples[N] = {};
		size_t _count = 0;
		size_t _index = 0;
	};
};
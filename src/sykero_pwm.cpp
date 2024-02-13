#include "mega.pch"
#include "sykero_pwm.hpp"

namespace sl
{
	pwm_chip::pwm_chip(
		const std::filesystem::path& path,
		uint8_t line_number,
		float frequency,
		float initial_percent) :
		line_path(path / ("pwm" + std::to_string(line_number)))
	{
		if (!std::filesystem::exists(line_path))
		{
			file_descriptor export_file(path / "export", O_WRONLY);
			export_file.write_text(std::to_string(line_number));
			export_file.close();

			std::this_thread::sleep_for(std::chrono::milliseconds(500));
		}

		_period.open(line_path / "period", O_WRONLY);
		_duty_cycle.open(line_path / "duty_cycle", O_WRONLY);

		set_frequency(frequency);
		set_duty_percent(initial_percent);

		file_descriptor enabled(line_path / "enable", O_WRONLY);
		enabled.write_text("1");

		syslog(LOG_INFO, "pwm_chip %s opened.", line_path.c_str());
	}

	pwm_chip::~pwm_chip()
	{
		syslog(LOG_INFO, "pwm_chip %s closed.", line_path.c_str());
	}

	void pwm_chip::set_frequency(float frequency)
	{
		if (frequency < 0)
		{
			throw std::invalid_argument("Frequency must be a positive floating point value!");
		}

		_period_ns = (1.0f / frequency) * 1000000000.0f;
		_period.write_text(std::to_string(_period_ns));
	}

	void pwm_chip::set_duty_percent(float percent)
	{
		if (percent < 0.0f || percent > 100.0f)
		{
			throw std::invalid_argument("Percentage must be between 0 and 100!");
		}

		if (_period_ns < 0)
		{
			throw std::logic_error("Set frequency first!");
		}

		std::string duty_cycle = std::to_string(int64_t(_period_ns * (percent / 100.0f)));

		_duty_cycle.write_text(duty_cycle);
	}
}
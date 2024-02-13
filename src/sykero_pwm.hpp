#pragma once

#include "sykero_io.hpp"

namespace sl
{
	class pwm_chip
	{
	public:
		pwm_chip(
			const std::filesystem::path& path,
			uint8_t line_number,
			float frequency,
			float initial_percent = 0);

		void set_frequency(float frequency);

		void set_duty_percent(float percent);

	private:
		int64_t _period_ns = 0;
		file_descriptor _period;
		file_descriptor _duty_cycle;
	};
}
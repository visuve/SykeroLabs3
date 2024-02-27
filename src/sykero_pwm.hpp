#pragma once

#include "sykero_io.hpp"

namespace sl::pwm
{
	class chip final
	{
	public:
		chip(
			const std::filesystem::path& path,
			uint8_t line_number,
			float frequency,
			float initial_percent = 0);
		~chip();

		SL_NON_COPYABLE(chip);

		void set_frequency(float frequency);

		void set_duty_percent(float percent);

	private:
		const std::filesystem::path line_path;
		int64_t _period_ns = 0;
		io::file_descriptor _period;
		io::file_descriptor _duty_cycle;
	};
}
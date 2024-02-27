#pragma once

#include "sykero_io.hpp"

namespace sl::gpio
{
	struct line_value_pair
	{
		constexpr inline line_value_pair(uint32_t offset, bool value = true) :
			offset(offset),
			value(value)
		{
		}

		constexpr auto operator <=> (const line_value_pair& other) const
		{
			return offset <=> other.offset;
		}

		constexpr bool operator == (const line_value_pair& other) const
		{
			return offset == other.offset;
		}

		const uint32_t offset;
		bool value;
	};

	class line_group final : private io::file_descriptor
	{
	public:
		line_group(int descriptor, const std::set<uint32_t>& offsets);
		SL_NON_COPYABLE(line_group);
		~line_group();

		void read_values(std::span<line_value_pair> data) const;
		void read_value(line_value_pair& lvp) const;
		bool read_event(gpio_v2_line_event& event) const;

		void write_values(std::span<line_value_pair> data) const;
		void write_value(const line_value_pair& lvp) const;

		template <typename Rep, typename Period>
		bool poll(std::chrono::duration<Rep, Period> time) const
		{
			return file_descriptor::poll(time, POLLIN | POLLPRI);
		}

	private:
		size_t index_of(uint32_t offset) const;

		std::set<uint32_t> _offsets;
	};

	class chip final : public io::file_descriptor
	{
	public:
		chip(const std::filesystem::path& path);
		SL_NON_COPYABLE(chip);
		~chip();

		gpio::line_group line_group(
			uint64_t flags,
			const std::set<uint32_t>& offsets,
			std::chrono::microseconds debounce = std::chrono::microseconds(0)) const;
	};

}
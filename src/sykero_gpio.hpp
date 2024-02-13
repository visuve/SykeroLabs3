#pragma once

#include "sykero_io.hpp"

namespace sl
{
	struct gpio_lvp // line value pair
	{
		// Clang needs this, otherwise pretty redundant
		constexpr inline gpio_lvp(uint32_t offset, bool value = true) :
			offset(offset),
			value(value)
		{
		}

		uint32_t offset = 0;
		bool value = true;
	};

	class gpio_line_group : private file_descriptor
	{
	public:
		gpio_line_group(int descriptor, const std::set<uint32_t>& offsets);
		~gpio_line_group();

		void read_values(std::span<gpio_lvp> data) const;
		void read_value(gpio_lvp& lvp) const;
		bool read_event(gpio_v2_line_event& event) const;

		void write_values(std::span<gpio_lvp> data) const;
		void write_value(const gpio_lvp& lvp) const;

		template <typename Rep, typename Period>
		bool poll(std::chrono::duration<Rep, Period> time) const
		{
			return file_descriptor::poll(time, POLLIN | POLLPRI);
		}

	private:
		size_t index_of(uint32_t offset) const;

		std::set<uint32_t> _offsets;
	};

	class gpio_chip : public file_descriptor
	{
	public:
		gpio_chip(const std::filesystem::path& path);
		~gpio_chip();

		gpio_line_group line_group(uint64_t flags, const std::set<uint32_t>& offsets) const;
	};

}
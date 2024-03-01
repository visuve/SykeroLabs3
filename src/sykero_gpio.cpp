#include "mega.pch"
#include "sykero_mem.hpp"
#include "sykero_gpio.hpp"
#include "sykero_log.hpp"

namespace sl::gpio
{
	line_group::line_group(int descriptor, const std::set<uint32_t>& offsets) :
		file_descriptor(descriptor),
		_offsets(offsets)
	{
		log_info("gpio::line_group %p opened.", this);
	}

	line_group::~line_group()
	{
		log_info("gpio::line_group %p closed.", this);
	}

	void line_group::read_values(std::span<line_value_pair> data) const
	{
		assert(data.size() <= _offsets.size());

		std::bitset<64> mask;
		std::bitset<64> bits;

		for (const line_value_pair& lvp : data)
		{
			size_t i = index_of(lvp.offset);
			mask.set(i, true);
		}

		gpio_v2_line_values values = { 0, mask.to_ullong() };
		file_descriptor::ioctl(GPIO_V2_LINE_GET_VALUES_IOCTL, &values);
		bits = values.bits;

		for (line_value_pair& lvp : data)
		{
			size_t i = index_of(lvp.offset);
			lvp.value = bits[i];
		}
	}

	void line_group::read_value(line_value_pair& lvp) const
	{
		line_value_pair data[1] = { lvp };
		read_values(data);
		lvp.value = data[0].value;
	}

	bool line_group::read_event(gpio_v2_line_event& event) const
	{
		return file_descriptor::read_value(event);
	}

	void line_group::write_values(std::span<line_value_pair> data) const
	{
		assert(data.size() <= _offsets.size());

		std::bitset<64> mask;
		std::bitset<64> bits;

		for (const line_value_pair& lvp : data)
		{
			size_t i = index_of(lvp.offset);
			mask.set(i, true);
			bits.set(i, lvp.value);
		}

		gpio_v2_line_values values = { bits.to_ullong(), mask.to_ullong() };
		file_descriptor::ioctl(GPIO_V2_LINE_SET_VALUES_IOCTL, &values);
	}

	void line_group::write_value(const line_value_pair& lvp) const
	{
		line_value_pair data[] = { lvp };
		write_values(data);
	}

	size_t line_group::index_of(uint32_t offset) const
	{
		auto iter = std::find(_offsets.cbegin(), _offsets.cend(), offset);

		if (iter == _offsets.cend())
		{
			throw std::invalid_argument("Offset not found");
		}

		return std::distance(_offsets.cbegin(), iter);
	}

	chip::chip(const std::filesystem::path& path) :
		file_descriptor(path)
	{
		log_info("gpio::chip %p opened. Path: %s", this, path.c_str());
	}

	chip::~chip()
	{
		log_info("gpio::chip %p closed.", this);
	}

	gpio::line_group chip::line_group(
		uint64_t flags,
		const std::set<uint32_t>& offsets,
		std::chrono::microseconds debounce) const
	{
		gpio_v2_line_config config;
		mem::clear(config);
		config.flags = flags;

		std::bitset<64> mask;

		if (debounce > std::chrono::microseconds(0))
		{
			config.num_attrs = 1;

			for (size_t i = 0; i < offsets.size(); ++i)
			{
				mask.set(i, true);
			}

			config.attrs[0].mask = mask.to_ullong();
			config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_DEBOUNCE;
			config.attrs[0].attr.debounce_period_us = debounce.count();
		}

		gpio_v2_line_request request;
		mem::clear(request);
		request.config = config;
		request.num_lines = offsets.size();

		mem::clone(offsets, request.offsets);
		mem::clone("sykerolabs", request.consumer);

		file_descriptor::ioctl(GPIO_V2_GET_LINE_IOCTL, &request);

		return gpio::line_group(request.fd, offsets);
	}
}
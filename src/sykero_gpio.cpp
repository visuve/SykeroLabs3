#include "mega.pch"
#include "sykero_mem.hpp"
#include "sykero_gpio.hpp"

namespace sl
{
	gpio_line_group::gpio_line_group(int descriptor, const std::set<uint32_t>& offsets) :
		file_descriptor(descriptor),
		_offsets(offsets)
	{
		syslog(LOG_INFO, "gpio_line_group %d opened.", _descriptor);
	}

	gpio_line_group::~gpio_line_group()
	{
		syslog(LOG_INFO, "gpio_line_group %d closed.", _descriptor);
	}

	void gpio_line_group::read_values(std::span<gpio_lvp> data) const
	{
		assert(data.size() <= _offsets.size());

		std::bitset<64> mask;
		std::bitset<64> bits;

		for (const gpio_lvp& lvp : data)
		{
			size_t i = index_of(lvp.offset);
			mask.set(i, true);
		}

		gpio_v2_line_values values = { 0, mask.to_ullong() };
		file_descriptor::ioctl(GPIO_V2_LINE_GET_VALUES_IOCTL, &values);
		bits = values.bits;

		for (gpio_lvp& lvp : data)
		{
			size_t i = index_of(lvp.offset);
			lvp.value = bits[i];
		}
	}

	void gpio_line_group::read_value(gpio_lvp& lvp) const
	{
		gpio_lvp data[1] = { lvp };
		read_values(data);
		lvp.value = data[0].value;
	}

	bool gpio_line_group::read_event(gpio_v2_line_event& event) const
	{
		return file_descriptor::read_value(event);
	}

	void gpio_line_group::write_values(std::span<gpio_lvp> data) const
	{
		assert(data.size() <= _offsets.size());

		std::bitset<64> mask;
		std::bitset<64> bits;

		for (const gpio_lvp& lvp : data)
		{
			size_t i = index_of(lvp.offset);
			mask.set(i, true);
			bits.set(i, lvp.value);
		}

		gpio_v2_line_values values = { bits.to_ullong(), mask.to_ullong() };
		file_descriptor::ioctl(GPIO_V2_LINE_SET_VALUES_IOCTL, &values);
	}

	void gpio_line_group::write_value(const gpio_lvp& lvp) const
	{
		gpio_lvp data[] = { lvp };
		write_values(data);
	}

	size_t gpio_line_group::index_of(uint32_t offset) const
	{
		auto iter = std::find(_offsets.cbegin(), _offsets.cend(), offset);

		if (iter == _offsets.cend())
		{
			throw std::invalid_argument("Offset not found");
		}

		return std::distance(_offsets.cbegin(), iter);
	}

	gpio_chip::gpio_chip(const std::filesystem::path& path) :
		file_descriptor(path)
	{
		syslog(LOG_INFO, "gpio_chip %d opened. Path: %s", _descriptor, path.c_str());
	}

	gpio_chip::~gpio_chip()
	{
		syslog(LOG_INFO, "gpio_chip %d closed.", _descriptor);
	}

	gpio_line_group gpio_chip::line_group(
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

		return gpio_line_group(request.fd, offsets);
	}
}
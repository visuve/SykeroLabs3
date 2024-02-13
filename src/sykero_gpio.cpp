#include "mega.pch"
#include "sykero_gpio.hpp"

namespace sl
{
	template <typename T, size_t TS, size_t SS>
	constexpr void clone(T(&target)[TS], const T(&source)[SS])
	{
		static_assert(TS >= SS, "Target size is smaller than destination!");

		for (size_t i = 0; i < SS; ++i)
		{
			target[i] = source[i];
		}
	}

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
		gpio_lvp data[] = { lvp };
		read_values(data);
		lvp = data[0];
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

	gpio_line_group gpio_chip::line_group(uint64_t flags, const std::set<uint32_t>& offsets) const
	{
		gpio_v2_line_config config = { 0 };
		config.flags = flags;

		gpio_v2_line_request request = { 0 };
		request.config = config;
		request.num_lines = offsets.size();

		size_t i = 0;

		for (uint32_t offset : offsets)
		{
			request.offsets[i] = offset;
			++i;
		}

		clone(request.consumer, "sykerolabs");

		file_descriptor::ioctl(GPIO_V2_GET_LINE_IOCTL, &request);

		return gpio_line_group(request.fd, offsets);
	}
}
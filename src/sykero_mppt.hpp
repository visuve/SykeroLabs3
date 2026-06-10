#pragma once

#include "sykero_io.hpp"
#include "sykero_props.hpp"

namespace sl::mppt
{
	class controller : public io::file_descriptor
	{
	public:
		controller(const std::filesystem::path& path);
		~controller() override = default;
		SL_NON_COPYABLE(controller);

		std::span<uint8_t> read_serial(std::span<uint8_t> buffer);
		bool parse(std::span<uint8_t> data);

		snapshot_average<float, BASE_MILLI> battery_voltage;
		snapshot_average<float, BASE_MILLI> battery_current;
		snapshot_average<float, BASE_MILLI> panel_voltage;
		snapshot_average<float, BASE_NONE> panel_power;
		snapshot_average<float, BASE_MILLI> load_current;

		snapshot<int, BASE_NONE> state;
		snapshot<int, BASE_NONE> error;

		snapshot<float, BASE_CENTI> yield_total;
		snapshot<int, BASE_NONE> max_power_today;

	private:
		enum class frame_state
		{
			HEADER,
			KEY,
			VALUE,
			CHECKSUM,
			DISCARD
		};

		enum class frame_event
		{
			PENDING,
			PAIR_READY,
			BLOCK_READY,
			CHECKSUM_MISMATCH
		};

		frame_event advance(uint8_t byte);
		
		frame_event handle_header_byte(uint8_t byte);
		frame_event handle_key_byte(uint8_t byte);
		frame_event handle_value_byte(uint8_t byte);
		frame_event handle_checksum_byte(uint8_t byte);
		frame_event handle_discard(uint8_t byte);

		void parse_pair();
		void commit_block();
		void undo_block();
		void reset();

		std::array<std::pair<std::string, property*>, 9> _properties;
		frame_state _state = frame_state::HEADER;
		std::string _key;
		std::string _value;
		uint8_t _checksum = 0;
	};
}
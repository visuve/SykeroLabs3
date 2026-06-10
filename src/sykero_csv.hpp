#pragma once

#include "sykero_io.hpp"

namespace sl::csv
{
	template <size_t COLUMNS>
	class file final : private io::file_descriptor
	{

	public:
		file(const std::filesystem::path& path, const std::array<std::string_view, COLUMNS>& header) :
			file_descriptor(),
			_header(header)
		{
			initialize(path);
		}

		SL_NON_COPYABLE(file);

		void initialize(const std::filesystem::path& path)
		{
			std::lock_guard<std::mutex> lock(_mutex);

			file_descriptor::open(path, O_WRONLY | O_CREAT | O_APPEND);

			for (auto column_name : _header)
			{
				append_value(column_name);
			}

			const size_t file_size = file_descriptor::file_size();

			if (file_size == 0)
			{
				// An empty file, write the header
				write_text(_row);
			}
			else if (file_size < _row.size())
			{
				// Malformed header, reopen, truncate and write header
				file_descriptor::open(path, O_WRONLY | O_TRUNC);
				write_text(_row);
			}

			_row.clear();
			_current_column = 0;

			log_info("%s opened.", path.c_str());
		}

		template<typename... Args>
		void append_row(Args&&... args)
		{
			static_assert(sizeof...(Args) == COLUMNS, "too few arguments!");

			std::lock_guard<std::mutex> lock(_mutex);

			(append_value(std::forward<Args>(args)), ...);
			write_text(_row);
			_row.clear();
			_current_column = 0;
			file_descriptor::fsync();
		}

	private:

		template <typename T>
		void append_value(T&& value)
		{
			if constexpr (std::is_arithmetic_v<std::remove_reference_t<T>>)
			{
				_row += std::to_string(value);
			}
			else
			{
				_row += value;
			}

			if (++_current_column < COLUMNS)
			{
				_row += ',';
			}
			else
			{
				_row += "\r\n";
			}
		}

		std::mutex _mutex;
		const std::array<std::string_view, COLUMNS> _header;
		size_t _current_column = 0;
		std::string _row;
	};
}
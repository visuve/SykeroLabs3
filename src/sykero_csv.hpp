#pragma once

#include "sykero_io.hpp"

namespace sl::csv
{
	template <size_t Columns>
	class file final : private io::file_descriptor
	{

	public:
		file(const std::filesystem::path& path, const std::array<std::string_view, Columns>& header) :
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
				append_text(column_name);
			}

			size_t file_size = file_descriptor::file_size();

			if (file_size > 0 && file_size < _row.size())
			{
				// Malformed header, reopen and truncate
				file_descriptor::open(path, O_WRONLY | O_TRUNC);
				file_size = 0;
			}

			if (file_size == 0)
			{
				// An empty file, write the header
				write_text(_row);
			}

			_row.clear();
			_current_column = 0;

			log_debug("csv %s initialized.", path.c_str());
		}

		template<typename... Args>
		void append_row(Args... args)
		{
			static_assert(sizeof...(Args) == Columns, "too few arguments!");

			std::lock_guard<std::mutex> lock(_mutex);

			(append_value(std::forward<Args>(args)), ...);
			write_text(_row);
			_row.clear();
			_current_column = 0;
			file_descriptor::fsync();
		}

	private:
		void append_text(std::string_view text)
		{
			_row += text;

			if (++_current_column < Columns)
			{
				_row += ',';
			}
			else
			{
				_row += "\r\n";
			}
		}

		template <typename T>
		void append_value(T t)
		{
			if constexpr (std::is_arithmetic_v<T>)
			{
				append_text(std::to_string(t));
			}
			else
			{
				append_text(t);
			}
		}

		std::mutex _mutex;
		const std::array<std::string_view, Columns> _header;
		size_t _current_column = 0;
		std::string _row;
	};
}
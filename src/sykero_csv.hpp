#pragma once

#include "sykero_io.hpp"

namespace sl
{
	template <size_t Columns>
	class csv_file : private file_descriptor
	{

	public:
		csv_file(const std::filesystem::path& path, const std::array<std::string_view, Columns>& header) :
			file_descriptor(path, O_RDWR | O_CREAT)
		{
			for (auto key : header)
			{
				append_text(key);
			}

			_current_column = 0;
		}

		template<typename... Args>
		void append_row(Args... args)
		{
			static_assert(sizeof...(Args) == Columns, "Too few arguments!");
			(append_value(std::forward<Args>(args)), ...);
			_current_column = 0;
		}

	private:
		inline void append_text(std::string_view text)
		{
			file_descriptor::write_text(text);

			if (++_current_column < Columns)
			{
				file_descriptor::write_text(",");
			}
			else
			{
				file_descriptor::write_text("\n");
			}
		}

		template <typename T>
		void append_value(T t)
		{
			append_text(std::to_string(t));
		}

		size_t _current_column = 0;
	};
}
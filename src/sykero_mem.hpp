#pragma once

namespace sl::mem
{
	template <typename T>
	constexpr void clear(T& x)
	{
		auto begin = reinterpret_cast<uint8_t*>(&x);
		auto end = reinterpret_cast<uint8_t*>(&x) + sizeof(T);

		while (begin < end)
		{
			*begin = 0;
			++begin;
		}
	}

	constexpr void clear(std::ranges::common_range auto& container)
	{
		std::fill(container.begin(), container.end(), 0);
	}

	template <typename T, size_t FS, size_t TS>
	constexpr void clone(const T(&from)[FS], T(&to)[TS])
	{
		static_assert(FS <= TS, "Will not fit!");

		for (size_t i = 0; i < FS; ++i)
		{
			to[i] = from[i];
		}
	}

	template <typename T, size_t TS>
	constexpr void clone(const std::set<T>& from, T(&to)[TS])
	{
		size_t i = 0;

		for (const T& x : from)
		{
			to[i] = x;
			++i;

			if (i >= TS)
			{
				throw std::out_of_range("Target size is smaller than source size!");
			}
		}
	}

	template <size_t FromSize, size_t ToSize, typename T>
	constexpr void join(const T(&from)[FromSize], T(&to)[ToSize])
	{
		static_assert(FromSize < ToSize, "Join from smaller to larger array. Otherwise use mem::clone.");

		constexpr size_t offset = ToSize - FromSize;

		for (size_t i = 0; i < FromSize; ++i)
		{
			to[i + offset] = from[i];
		}
	}
}
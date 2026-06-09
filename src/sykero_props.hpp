#pragma once

namespace sl
{
	constexpr float MULT_MILLI = 0.001f;
	constexpr float MULT_CENTI = 0.01f;
	constexpr float MULT_ONE_F = 1.0f;
	constexpr int   MULT_ONE_I = 1;

	template<typename T>
	concept arithmetic = std::is_arithmetic_v<T>;

	class property
	{
	public:
		virtual void parse(std::string_view value) = 0;
		virtual void commit() = 0;
		virtual void undo() = 0;
		virtual ~property() = default;
	};

	// A moving average property that maintains a circular buffer of the last N samples and calculates the average on demand.
	template<arithmetic T, size_t N, T multiplier = static_cast<T>(1)>
	class rolling_average : public property
	{
	public:
		static_assert(N > 0, "N must be greater than zero");

		void parse(std::string_view value) override
		{
			T parsed = T(0);
			auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
			
			if (ec == std::errc())
			{
				_stage = parsed * multiplier;
			}
		}

		void commit() override
		{
			if (_stage.has_value())
			{
				update(_stage.value());
				_stage.reset();
			}
		}

		void undo() override
		{
			_stage.reset();
		}

		void update(T val)
		{
			_buffer[_head] = val;
			_head = (_head + 1) % N;
			
			if (_count < N)
			{
				_count++;
			}
		}

		T get() const
		{
			if (!_count)
			{
				throw std::logic_error("cannot calculate the moving average of zero samples");
			}

			T sum = T(0);

			for (size_t i = 0; i < _count; ++i)
			{
				sum += _buffer[i];
			}
			
			return sum / static_cast<T>(_count);
		}

	private:
		std::array<T, N> _buffer;
		size_t _head = 0;
		size_t _count = 0;
		std::optional<T> _stage;
	};

	// A sample average property that maintains a running sum and count of samples, and calculates the average on demand. The average is reset after each get() call.
	template <arithmetic T, T multiplier = static_cast<T>(1)>
	class snapshot_average : public property
	{
	public:
		void parse(std::string_view value) override
		{
			T parsed = T(0);

			auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
			
			if (ec == std::errc())
			{
				_stage = parsed * multiplier;
			}
		}

		void commit() override
		{
			if (_stage.has_value())
			{
				update(_stage.value());
				_stage.reset();
			}
		}

		void undo() override
		{
			_stage.reset();
		}

		void update(T val)
		{
			_sum += val;
			_count++;
		}

		T get()
		{
			if (!_count)
			{
				throw std::logic_error("cannot calculate the sample average of zero samples");
			}

			_last = _sum / static_cast<T>(_count);
			_sum = T(0);
			_count = 0;
			
			return _last;
		}
	private:
		T _sum = T(0);
		size_t _count = 0;
		T _last = T(0);
		std::optional<T> _stage;
	};

	// A snapshot property that stores the most recently parsed value and only updates the current value when commit() is called. The undo() function discards the snapshot.
	template <arithmetic T, T multiplier = static_cast<T>(1)>
	class snapshot : public property
	{
	public:
		void parse(std::string_view value) override
		{
			T parsed = T(0);
			auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
			
			if (ec == std::errc())
			{
				_stage = parsed * multiplier;
			}
		}

		void commit() override
		{
			if (_stage.has_value())
			{
				update(_stage.value());
				_stage.reset();
			}
		}

		void undo() override
		{
			_stage.reset();
		}

		void update(T val)
		{
			_value = val;
		}

		T get() const
		{
			return _value;
		}

	private:
		T _value = T(0);
		std::optional<T> _stage;
	};
}
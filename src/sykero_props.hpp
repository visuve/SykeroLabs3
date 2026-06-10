#pragma once

namespace sl
{
	constexpr uint32_t BASE_NONE = 1;
	constexpr uint32_t BASE_DECI = 10;
	constexpr uint32_t BASE_CENTI = 100;
	constexpr uint32_t BASE_MILLI = 1000;
	constexpr uint32_t BASE_MICRO = 1'000'000;

	class property
	{
	public:
		virtual property& parse(std::string_view) = 0;
		virtual void commit() = 0;
		virtual void undo() = 0;
		virtual void reset() = 0;
		virtual ~property() = default;
	};

	template<typename T>
	concept arithmetic = std::is_arithmetic_v<T>;

	template <arithmetic T, uint32_t DIVIDER = BASE_NONE>
	class property_base : public property
	{
	public:
		static_assert(DIVIDER > 0, "DIVIDER must be greater than zero");

		virtual T get() = 0;
		virtual void update(T) = 0;

		property& parse(std::string_view value) override
		{ 
			std::lock_guard<std::recursive_mutex> lock(this->_mutex);

			T parsed = static_cast<T>(0);
			auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);

			if (ec == std::errc())
			{
				_stage = parsed / static_cast<T>(DIVIDER);
			}

			return *this;
		}

		void commit() override
		{
			std::lock_guard<std::recursive_mutex> lock(this->_mutex);

			if (_stage.has_value())
			{
				update(_stage.value());
				_stage.reset();
			}
		}

		void undo() override
		{
			std::lock_guard<std::recursive_mutex> lock(this->_mutex);
			_stage.reset();
		}

	protected:
		std::recursive_mutex _mutex;
		std::optional<T> _stage;
	};

	// A moving average property that maintains a circular buffer of the last N samples and calculates the average on demand.
	template<size_t N, arithmetic T, uint32_t DIVIDER = BASE_NONE>
	class rolling_average : public property_base<T, DIVIDER>
	{
	public:
		static_assert(N > 0, "N must be greater than zero");

		void update(T val) override
		{
			std::lock_guard<std::recursive_mutex> lock(this->_mutex);
			_buffer[_head] = val;
			_head = (_head + 1) % N;
			
			if (_count < N)
			{
				_count++;
			}
		}

		T get() override
		{
			std::lock_guard<std::recursive_mutex> lock(this->_mutex);

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

		void reset() override
		{
			std::lock_guard<std::recursive_mutex> lock(this->_mutex);
			_buffer.fill(T(0));
			_head = 0;
			_count = 0;
			this->_stage.reset();
		}

	private:
		std::array<T, N> _buffer;
		size_t _head = 0;
		size_t _count = 0;
	};

	// A sample average property that maintains a running sum and count of samples, and calculates the average on demand. The average is reset after each get() call.
	template <arithmetic T, uint32_t DIVIDER = BASE_NONE>
	class snapshot_average : public property_base<T, DIVIDER>
	{
	public:
		static_assert(DIVIDER > 0, "DIVIDER must be greater than zero");

		void update(T val) override
		{
			std::lock_guard<std::recursive_mutex> lock(this->_mutex);
			_sum += val;
			_count++;
		}

		T get() override
		{
			std::lock_guard<std::recursive_mutex> lock(this->_mutex);

			if (!_count)
			{
				throw std::logic_error("cannot calculate the sample average of zero samples");
			}

			_last = _sum / static_cast<T>(_count);
			_sum = static_cast<T>(0);
			_count = 0;
			
			return _last;
		}

		void reset() override
		{
			std::lock_guard<std::recursive_mutex> lock(this->_mutex);
			_sum = static_cast<T>(0);
			_count = 0;
			_last = static_cast<T>(0);
			this->_stage.reset();
		}

	private:
		T _sum = static_cast<T>(0);
		size_t _count = 0;
		T _last = static_cast<T>(0);
	};

	// A snapshot property that stores the most recently parsed value and only updates the current value when commit() is called. The undo() function discards the snapshot.
	template <arithmetic T, uint32_t DIVIDER = BASE_NONE>
	class snapshot : public property_base<T, DIVIDER>
	{
	public:
		static_assert(DIVIDER > 0, "DIVIDER must be greater than zero");

		void update(T val) override
		{
			std::lock_guard<std::recursive_mutex> lock(this->_mutex);
			_value = val;
		}

		T get() override
		{
			std::lock_guard<std::recursive_mutex> lock(this->_mutex);
			return _value;
		}

		void reset() override
		{
			std::lock_guard<std::recursive_mutex> lock(this->_mutex);
			_value = static_cast<T>(0);
			this->_stage.reset();
		}

	private:
		T _value = static_cast<T>(0);
	};
}
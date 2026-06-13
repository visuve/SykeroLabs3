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

	template <typename T>
	class property_group
	{
	public:
		class lock_ref
		{
		public:
			lock_ref(std::mutex& mutex, T& data) :
				_lock(mutex),
				_data(data)
			{
			}

			T* operator->()
			{
				return &_data;
			}

		private:
			std::unique_lock<std::mutex> _lock;
			T& _data;
		};

		lock_ref acquire()
		{
			return lock_ref(_mutex, _data);
		}

	private:
		mutable std::mutex _mutex;
		T _data;

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

	protected:
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
			_buffer[_head] = val;
			_head = (_head + 1) % N;
			
			if (_count < N)
			{
				_count++;
			}
		}

		T get() override
		{
			if (!_count)
			{
				return static_cast<T>(0);
			}

			T sum = static_cast<T>(0);

			for (size_t i = 0; i < _count; ++i)
			{
				sum += _buffer[i];
			}

			return sum / static_cast<T>(_count);
		}

		void reset() override
		{
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
			_sum += val;
			_count++;
		}

		T get() override
		{
			if (_count)
			{
				_last = _sum / static_cast<T>(_count);
				_sum = static_cast<T>(0);
				_count = 0;
			}
			
			return _last;
		}

		void reset() override
		{
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
			_value = val;
		}

		T get() override
		{
			return _value;
		}

		void reset() override
		{
			_value = static_cast<T>(0);
			this->_stage.reset();
		}

	private:
		T _value = static_cast<T>(0);
	};

	template <typename T, typename DURATION>
	class frequency_counter
	{
	public:
		void reset()
		{
			_counter = ZERO;
		}

		void update(std::chrono::nanoseconds time)
		{
			if (_counter == ZERO)
			{
				_start = time;
			}

			++_counter;
		}

		T get(std::chrono::nanoseconds end) const
		{
			const std::chrono::nanoseconds time_taken(end - _start);

			if (time_taken.count() < 0)
			{
				return static_cast<T>(0);
			}

			const T time = static_cast<T>(time_taken.count());
			return (_counter / time) * FREQUENCY;
		}

	private:
		static constexpr T ZERO = static_cast<T>(0);

		static constexpr T FREQUENCY =
			static_cast<T>(std::chrono::duration_cast<std::chrono::nanoseconds>(DURATION(1)).count());

		std::chrono::nanoseconds _start = std::chrono::nanoseconds::zero();
		T _counter = ZERO;
	};
}
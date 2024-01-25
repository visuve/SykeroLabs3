#include "sykerolabs.hpp"

class GPIO
{
public:
	GPIO(const char* name, uint32_t line_count) :
		_lines(line_count)
	{
		_chip = gpiod_chip_open_by_name(name);
	}

	~GPIO()
	{
		for (gpiod_line* line : _lines)
		{
			if (line)
			{
				gpiod_line_release(line);
			}
		}

		if (_chip)
		{
			gpiod_chip_close(_chip);
		}
	}

	gpiod_line* operator[] (uint32_t i)
	{
		if (i >= _lines.size())
		{
			return nullptr;
		}

		if (!_lines[i])
		{
			_lines[i] = gpiod_chip_get_line(_chip, i);
		}

		return _lines[i];
	}

private:
	gpiod_chip* _chip = nullptr;
	std::vector<gpiod_line*> _lines;
};

int main()
{
	// run e.g. gpioinfo
	GPIO gpio("gpiochip4", 54);

	gpiod_line* test = gpio[5];

	if (gpiod_line_request_input(test, "example1") < 0)
	{
		perror("Request line as input failed\n");
		return 1;
	}

	for (int i = 0; i < 60; ++i)
	{
		int value = gpiod_line_get_value(test);

		if (value < 0)
		{
			perror("Read line input failed\n");
			return 1;
		}

		printf("value=%d\n", value);

		usleep(1000000);
	}
	
	return 0;
}
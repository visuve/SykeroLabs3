# SykeröLabs3

- Automating my [NFT (Nutrient Film Technique)](https://en.wikipedia.org/wiki/Nutrient_film_technique) driven greenhouse
- 3rd iteration made with [Raspberry Pi 5](https://www.raspberrypi.com/products/raspberry-pi-5/) & [C++](https://en.wikipedia.org/wiki/C%2B%2B) on [Raspberry Pi OS](https://www.raspberrypi.com/software/)
	- See previous (2nd version) on Raspberry Pico in https://github.com/visuve/SykeroLabs2 
- See [Sykerolabs 3 wiki](https://github.com/visuve/SykeroLabs3/wiki) for technical illustrations and pictures i.e. the full documentation
	- The documentation below is only for building, debugging etc. the application itself

## Building

- Sykerolabs3 uses [CMake](https://cmake.org/) you need to install it first
- You can use [GCC](https://gcc.gnu.org/) or [Clang](https://clang.llvm.org/) on your Raspberry Pi
	- Note that GCC is installed by default on Raspberry Pi OS and the Clang builds have not been tested 
	- See examples [gcc.yml](https://github.com/visuve/SykeroLabs3/blob/master/.github/workflows/gcc.yml) or [clang.yml](https://github.com/visuve/SykeroLabs3/blob/master/.github/workflows/gcc.yml) how to build the project
- At the moment Sykerolabs uses no other than C++ standard libraries, i.e. there are no dependencies
- I use Visual Studio with the *"Linux and embedded development with C++"* workload
	- Sometimes I use [Windows Subsystem for Linux (WSL)](https://learn.microsoft.com/en-us/windows/wsl/about) when developing stuff that does not need [GPIO](https://en.wikipedia.org/wiki/General-purpose_input/output) related things

## Debugging

- The application produces [CSV data](https://en.wikipedia.org/wiki/Comma-separated_values) of the states of the attached relays & probes
	- On release builds the data goes into timestamped files in ``~/sykerolabs`` and on debug builds the data is printed to console
- The application also produces event logs which can be viewed with ``journalctl -f -t sykerolabs`` for debugging purposes
	- Assuming you use systemd on the target OS (which is default on the Raspberry Pi OS)...
	- See https://www.freedesktop.org/software/systemd/man/latest/journalctl.html for more datails

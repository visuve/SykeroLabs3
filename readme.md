# SykeröLabs3

- Automating my [NFT (Nutrient Film Technique)](https://en.wikipedia.org/wiki/Nutrient_film_technique) driven greenhouse
- 3rd iteration made with Raspberry Pi & C++
	- See previous in https://github.com/visuve/SykeroLabs2
- See [wiki](https://github.com/visuve/SykeroLabs3/wiki) for technical illustrations and pictures i.e. the full documentation
	- The documentation below is only for building, debugging etc. the application itself

## Building

- You can use GCC or Clang on your Raspberry Pi
	- See [gcc.yml](https://github.com/visuve/SykeroLabs3/blob/master/.github/workflows/gcc.yml) or [clang.yml](https://github.com/visuve/SykeroLabs3/blob/master/.github/workflows/gcc.yml) how to build
- At the moment Sykerolabs uses no other than C++ standard libraries, i.e. there are no dependencies
- I use Visual Studio with the *"Linux and embedded development with C++"* workload
	- Sometimes I use [Windows Subsystem for Linux (WSL)](https://learn.microsoft.com/en-us/windows/wsl/about) when developing stuff that does not need [GPIO](https://en.wikipedia.org/wiki/General-purpose_input/output) related things

## Debugging

- The application logs can be viewed with ``journalctl -f -t sykerolabs``.
	- Assuming you use systemd on the target OS

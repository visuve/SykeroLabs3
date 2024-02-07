file(GLOB sykerolabs_src "*.cpp")

add_executable(sykerolabs ${sykerolabs_src})
target_precompile_headers(sykerolabs PRIVATE "mega.pch")
target_link_libraries(sykerolabs gpiodcxx) # apt install libgpiod-dev

if (DEFINED $ENV{SHELL})
	add_compile_definitions(RASPBERRY)
endif()
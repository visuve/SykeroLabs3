file(GLOB sykerolabs_src "*.cpp")

add_executable(sykerolabs ${sykerolabs_src})
# Syntax highlight tends to break with PCHs in Visual Studio
# target_precompile_headers(sykerolabs PRIVATE "mega.pch")
target_link_libraries(sykerolabs gpiodcxx) # apt install libgpiod-dev

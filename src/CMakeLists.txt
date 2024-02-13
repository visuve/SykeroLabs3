file(GLOB sykerolabs_src "*.cpp")

add_executable(sykerolabs ${sykerolabs_src})
target_precompile_headers(sykerolabs PRIVATE "mega.pch")
target_link_libraries(sykerolabs)

install(TARGETS sykerolabs)
file(GLOB sykerolabs_src "*.cpp")

add_executable(sykerolabs ${sykerolabs_src})

set(BASE_MODEL_PATH "/sys/firmware/devicetree/base/model")

if (EXISTS ${BASE_MODEL_PATH})
	file(READ ${BASE_MODEL_PATH} RPI_BASE_MODEL)
	message("RPI_BASE_MODEL=${RPI_BASE_MODEL}")

	if (${RPI_BASE_MODEL} MATCHES "Raspberry Pi Zero 2 W")
		add_compile_definitions(SYKEROLABS_RPIZ2W)
	elseif(${RPI_BASE_MODEL} MATCHES "Raspberry Pi 5")
		add_compile_definitions(SYKEROLABS_RPI5)
	else()
		message(SEND_ERROR "Unsupported platform for Sykerolabs3!")
	endif()
else()
	message(WARNING "${BASE_MODEL_PATH} not found.")
	add_compile_definitions(SYKEROLABS_RPI5)
endif()

target_precompile_headers(sykerolabs PRIVATE "mega.pch")
target_link_libraries(sykerolabs)

install(TARGETS sykerolabs DESTINATION "~/sykerolabs")
install(FILES sykerolabs.service DESTINATION "~/.config/systemd/user")
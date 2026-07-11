cmake_minimum_required(VERSION 3.16)

project(standalone_apps)

add_subdirectory(uart)

add_custom_target(
	standalone_apps
	DEPENDS uart_app
)


project(pilight_lua_c)

set(${PROJECT_NAME}_files "" CACHE INTERNAL "")

add_subdirectory(config)
add_subdirectory(network)
add_subdirectory(io)

file(GLOB ${PROJECT_NAME}_sources "${PROJECT_SOURCE_DIR}/*.c")
file(GLOB ${PROJECT_NAME}_headers "${PROJECT_SOURCE_DIR}/*.h")

set(${PROJECT_NAME}_files
	${${PROJECT_NAME}_sources}
	${${PROJECT_NAME}_headers}
	${${PROJECT_NAME}_config_files}
	${${PROJECT_NAME}_network_files}
	${${PROJECT_NAME}_io_files}
CACHE INTERNAL "")
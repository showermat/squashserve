cmake_minimum_required(VERSION 3.12)
project(zsr-utils)
set(VERSION 0.2)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "-Wall")
include_directories("." "/usr/include/libxml2")
include_directories("." "../lib/json/include")
add_subdirectory(util)
add_library(zsr zsr/zsr.cpp zsr/compress.cpp zsr/writer.cpp)
target_link_libraries(zsr util lzma pthread)
add_executable(zsrutil zsrutil.cpp)
target_link_libraries(zsrutil zsr)
add_executable(zsrmnt zsrmnt.cpp)
target_link_libraries(zsrmnt zsr fuse)
add_custom_command(
	OUTPUT ${CMAKE_SOURCE_DIR}/fileinclude.cpp
	OUTPUT ${CMAKE_SOURCE_DIR}/fileinclude.h
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	COMMAND util/fileinclude.sh ${CMAKE_SOURCE_DIR}/fileinclude vollib.lua prefs.json
)
add_executable(mkvol mkvol.cpp search.cpp Volume.cpp ${CMAKE_SOURCE_DIR}/fileinclude.cpp) # xapian
target_link_libraries(mkvol zsr)
add_custom_command(
	OUTPUT ${CMAKE_BINARY_DIR}/resources.zsr
	DEPENDS zsrutil
	COMMAND zsrutil c ${CMAKE_SOURCE_DIR}/../resources ${CMAKE_BINARY_DIR}/resources.zsr ${CMAKE_SOURCE_DIR}/../resources/info.lua
)
add_custom_target(resources DEPENDS ${CMAKE_BINARY_DIR}/resources.zsr)
add_executable(zsrsrv zsrsrv.cpp search.cpp Volume.cpp http.cpp ${CMAKE_SOURCE_DIR}/fileinclude.cpp)
add_dependencies(zsrsrv resources)
target_link_libraries(zsrsrv util zsr mongoose pthread) # xapian

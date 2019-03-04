##
# CMake minimum required version for the project.
##
cmake_minimum_required(VERSION 3.11)

##
# tutorial02 C Project CMakeLists.txt.
##
project(create_window C)

##
# Sets the C standard whose features are requested to build this target.
##
set(CMAKE_C_STANDARD 99)

##
# Adds create_window.c executable target.
##
add_executable(create_window create_window.c)

##
# Adds include directories to be used when compiling and libraries to be used when
# linking target create_window.
##
target_include_directories(create_window PRIVATE ${FFMPEG_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS})
target_link_libraries(create_window PRIVATE ${FFMPEG_LIBRARIES} ${SDL2_LIBRARIES})


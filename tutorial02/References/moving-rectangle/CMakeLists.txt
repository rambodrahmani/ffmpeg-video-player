##
# CMake minimum required version for the project.
##
cmake_minimum_required(VERSION 3.11)

##
# tutorial02 C Project CMakeLists.txt.
##
project(moving_rectangle C)

##
# Sets the C standard whose features are requested to build this target.
##
set(CMAKE_C_STANDARD 99)

##
# Adds moving_rectangle.c executable target.
##
add_executable(moving_rectangle moving_rectangle.c)

##
# Adds include directories to be used when compiling and libraries to be used when
# linking target moving_rectangle.
##
target_include_directories(moving_rectangle PRIVATE ${FFMPEG_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS})
target_link_libraries(moving_rectangle PRIVATE ${FFMPEG_LIBRARIES} ${SDL2_LIBRARIES})
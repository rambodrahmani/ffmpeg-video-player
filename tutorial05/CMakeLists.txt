##
# CMake minimum required version for the project.
##
cmake_minimum_required(VERSION 3.11)

##
# tutorial05 C Project CMakeLists.txt.
##
project(tutorial05 C)

##
# Sets the C standard whose features are requested to build this target.
##
set(CMAKE_C_STANDARD 99)

##
# Adds tutorial05.c executable target.
##
add_executable(tutorial05 tutorial05.c)

##
# Adds include directories to be used when compiling and libraries to be used when
# linking target tutorial05.
##
target_include_directories(tutorial05 PRIVATE ${FFMPEG_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS})
target_link_libraries(tutorial05 PRIVATE ${FFMPEG_LIBRARIES} ${SDL2_LIBRARIES} m)

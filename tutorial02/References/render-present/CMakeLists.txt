##
# CMake minimum required version for the project.
##
cmake_minimum_required(VERSION 3.11)

##
# tutorial02 C Project CMakeLists.txt.
##
project(render_present C)

##
# Sets the C standard whose features are requested to build this target.
##
set(CMAKE_C_STANDARD 99)

##
# Adds render_present.c executable target.
##
add_executable(render_present render_present.c)

##
# Adds include directories to be used when compiling and libraries to be used when
# linking target render_present.
##
target_include_directories(render_present PRIVATE ${FFMPEG_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS})
target_link_libraries(render_present PRIVATE ${FFMPEG_LIBRARIES} ${SDL2_LIBRARIES})
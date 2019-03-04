##
# CMake minimum required version for the project.
##
cmake_minimum_required(VERSION 3.11)

##
# player C Project CMakeLists.txt.
##
project(player C)

##
# Sets the C standard whose features are requested to build this target.
##
set(CMAKE_C_STANDARD 99)

##
# Adds player-sdl2.c executable target.
##
add_executable(player-sdl player-sdl.c)

##
# Adds include directories to be used when compiling and libraries to be used when
# linking target player-sdl.
##
target_include_directories(player-sdl PRIVATE ${FFMPEG_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS})
target_link_libraries(player-sdl PRIVATE ${FFMPEG_LIBRARIES} ${SDL2_LIBRARIES} m)

##
# Adds player-sdl2.c executable target.
##
add_executable(player-sdl2 player-sdl2.c)

##
# Adds include directories to be used when compiling and libraries to be used when
# linking target player-sdl.
##
target_include_directories(player-sdl2 PRIVATE ${FFMPEG_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS})
target_link_libraries(player-sdl2 PRIVATE ${FFMPEG_LIBRARIES} ${SDL2_LIBRARIES} m)


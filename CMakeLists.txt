##
# CMake minimum required version for the project.
##
cmake_minimum_required(VERSION 3.11)

##
# FFmpeg-Video-Player C Project CMakeLists.txt.
##
project(FFmpeg_Video_Player C)

##
# Include module pkg-config for CMake.
##
INCLUDE(FindPkgConfig)

##
# CMAKE_MODULE_PATH:FILEPATH=./FFmpeg-Video-Player/Modules
##
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/Modules)

##
# Find FFmpeg include directories and link libraries.
##
find_package(FFMPEG REQUIRED)

##
# Find SDL include directories and link libraries.
##
find_package(SDL REQUIRED)

##
# Find SDL2 include directories and link libraries.
##
find_package(SDL2 REQUIRED)

##
# Add subdirectories to the build.
##
add_subdirectory(tutorial01)
add_subdirectory(tutorial02)
add_subdirectory(tutorial03)
add_subdirectory(tutorial04)
add_subdirectory(tutorial05)
add_subdirectory(tutorial06)
add_subdirectory(tutorial07)
add_subdirectory(player)

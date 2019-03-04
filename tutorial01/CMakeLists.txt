##
# CMake minimum required version for the project.
##
cmake_minimum_required(VERSION 3.11)

##
# tutorial01 C Project CMakeLists.txt.
##
project(tutorial01 C)

##
# Sets the C standard whose features are requested to build this target.
##
set(CMAKE_C_STANDARD 99)

##
# Adds tutorial01-deprecated.c executable target.
##
add_executable(tutorial01-deprecated tutorial01-deprecated.c)

##
# Adds include directories to be used when compiling and libraries to be used
# when linking target tutorial01-deprecated.
##
target_include_directories(tutorial01-deprecated PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(tutorial01-deprecated PRIVATE ${FFMPEG_LIBRARIES})

##
# Adds tutorial01.c executable target.
##
add_executable(tutorial01 tutorial01.c)

##
# Adds include directories to be used when compiling and libraries to be used
# when linking target tutorial01.
##
target_include_directories(tutorial01 PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(tutorial01 PRIVATE ${FFMPEG_LIBRARIES})


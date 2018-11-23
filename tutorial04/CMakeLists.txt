##
# CMake minimum required version for the project.
##
cmake_minimum_required(VERSION 3.11)

##
# tutorial04 C Project CMakeLists.txt.
##
project(tutorial04 C)

##
# Sets the C standard whose features are requested to build this target.
##
set(CMAKE_C_STANDARD 99)

##
# Adds tutorial04-deprecated.c executable target.
##
add_executable(tutorial04-deprecated tutorial04-deprecated.c)

##
# Adds include directories to be used when compiling and libraries to be used when
# linking target tutorial04-deprecated.
##
target_include_directories(tutorial04-deprecated PRIVATE ${FFMPEG_INCLUDE_DIRS} ${SDL_INCLUDE_DIRS})
target_link_libraries(tutorial04-deprecated PRIVATE ${FFMPEG_LIBRARIES} ${SDL_LIBRARIES} m)

##
# Adds tutorial03-resampled-deprecated.c executable target.
##
add_executable(tutorial04-resampled-deprecated tutorial04-resampled-deprecated.c)

##
# Adds include directories to be used when compiling and libraries to be used when
# linking target tutorial04-resampled-deprecated.
##
target_include_directories(tutorial04-resampled-deprecated PRIVATE ${FFMPEG_INCLUDE_DIRS} ${SDL_INCLUDE_DIRS})
target_link_libraries(tutorial04-resampled-deprecated PRIVATE ${FFMPEG_LIBRARIES} ${SDL_LIBRARIES} m)

##
# Adds tutorial03-resampled.c executable target.
##
add_executable(tutorial04-resampled tutorial04-resampled.c)

##
# Adds include directories to be used when compiling and libraries to be used when
# linking target tutorial04-resampled.
##
target_include_directories(tutorial04-resampled PRIVATE ${FFMPEG_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS})
target_link_libraries(tutorial04-resampled PRIVATE ${FFMPEG_LIBRARIES} ${SDL2_LIBRARIES} m)
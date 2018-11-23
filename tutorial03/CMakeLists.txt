##
# CMake minimum required version for the project.
##
cmake_minimum_required(VERSION 3.11)

##
# tutorial03 C Project CMakeLists.txt.
##
project(tutorial03 C)

##
# Sets the C standard whose features are requested to build this target.
##
set(CMAKE_C_STANDARD 99)

##
# Adds tutorial03-deprecated.c executable target.
##
add_executable(tutorial03-deprecated tutorial03-deprecated.c)

##
# Adds include directories to be used when compiling and libraries to be used when
# linking target tutorial03-deprecated.
##
target_include_directories(tutorial03-deprecated PRIVATE ${FFMPEG_INCLUDE_DIRS} ${SDL_INCLUDE_DIRS})
target_link_libraries(tutorial03-deprecated PRIVATE ${FFMPEG_LIBRARIES} ${SDL_LIBRARIES})

##
# Adds tutorial03-resampled-deprecated.c executable target.
##
add_executable(tutorial03-resampled-deprecated tutorial03-resampled-deprecated.c)

##
# Adds include directories to be used when compiling and libraries to be used when
# linking target tutorial03-resampled-deprecated.
##
target_include_directories(tutorial03-resampled-deprecated PRIVATE ${FFMPEG_INCLUDE_DIRS} ${SDL_INCLUDE_DIRS})
target_link_libraries(tutorial03-resampled-deprecated PRIVATE ${FFMPEG_LIBRARIES} ${SDL_LIBRARIES})

##
# Adds tutorial03-resampled.c executable target.
##
add_executable(tutorial03-resampled tutorial03-resampled.c)

##
# Adds include directories to be used when compiling and libraries to be used when
# linking target tutorial03-resampled.
##
target_include_directories(tutorial03-resampled PRIVATE ${FFMPEG_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS})
target_link_libraries(tutorial03-resampled PRIVATE ${FFMPEG_LIBRARIES} ${SDL2_LIBRARIES})
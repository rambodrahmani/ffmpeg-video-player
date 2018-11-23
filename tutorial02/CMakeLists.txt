##
# CMake minimum required version for the project.
##
cmake_minimum_required(VERSION 3.11)

##
# tutorial02 C Project CMakeLists.txt.
##
project(tutorial02 C)

##
# Sets the C standard whose features are requested to build this target.
##
set(CMAKE_C_STANDARD 99)

##
# Adds tutorial02-deprecated.c executable target.
##
add_executable(tutorial02-deprecated tutorial02-deprecated.c)

##
# Adds include directories to be used when compiling and libraries to be used when
# linking target tutorial02-deprecated.
##
target_include_directories(tutorial02-deprecated PRIVATE ${FFMPEG_INCLUDE_DIRS} ${SDL_INCLUDE_DIRS})
target_link_libraries(tutorial02-deprecated PRIVATE ${FFMPEG_LIBRARIES} ${SDL_LIBRARIES})

##
# Adds tutorial02.c executable target.
##
add_executable(tutorial02 tutorial02.c)

##
# Adds include directories to be used when compiling and libraries to be used when
# linking target tutorial02.
##
target_include_directories(tutorial02 PRIVATE ${FFMPEG_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS})
target_link_libraries(tutorial02 PRIVATE ${FFMPEG_LIBRARIES} ${SDL2_LIBRARIES})

##
# Add subdirectories to the build.
##
add_subdirectory(References/create-window)
add_subdirectory(References/moving-rectangle)
add_subdirectory(References/render-present)
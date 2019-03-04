##
# Find SDL Libraries.
#
# Once done this will define
#   SDL_FOUND        - System has the all required components.
#   SDL_INCLUDE_DIRS - Include directory necessary for using the required components headers.
#   SDL_LIBRARIES    - Link these to use the required ffmpeg components.
##
PKG_SEARCH_MODULE(SDL REQUIRED sdl)


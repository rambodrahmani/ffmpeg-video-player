# Player
The following directory contains a full FFmpeg player forked from the original
[FFplay](https://ffmpeg.org/ffplay.html). FFplay is a very simple and portable
media player using the FFmpeg libraries and the SDL library. It is mostly used
as a testbed for the various FFmpeg APIs.

The main difference with the original FFplay lies in the fact that I did my best
in order to avoid deprecated APIs (both as far as it concerns FFmpeg and SDL).
Additionally, while the Ffplay uses SDL, this fork uses SDL2.

As you can see, two version of the player are provided: **player-sdl2.c** is the
SDL2 based fork of FFplay, while **player-glfw.c** uses GLFW instead of SDL2.


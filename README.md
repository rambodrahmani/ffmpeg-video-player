# FFmpeg Video Player
<!-- PROJECT SHIELDS -->
<p align="center">
    <a href="#">
    <img src="https://img.shields.io/github/languages/top/rambodrahmani/ffmpeg-video-player.svg?logo=github" alt="Github Top Languages">
    <a href="#">
    <img src="https://img.shields.io/github/downloads/rambodrahmani/ffmpeg-video-player/total.svg?logo=github" alt="Github Donwloads">
    <a href="https://github.com/rambodrahmani/ffmpeg-video-player/commits/master">
    <img src="https://img.shields.io/github/last-commit/rambodrahmani/ffmpeg-video-player.svg?logo=github" alt="GitHub last commit">
    <a href="https://github.com/rambodrahmani/ffmpeg-video-player/issues">
    <img src="https://img.shields.io/github/issues-raw/rambodrahmani/ffmpeg-video-player.svg?logo=github" alt="GitHub issues">
    <a href="https://github.com/rambodrahmani/ffmpeg-video-player/pulls">
    <img src="https://img.shields.io/github/issues-pr-raw/rambodrahmani/ffmpeg-video-player.svg?logo=github" alt="GitHub pull requests">
    <a href="https://gitter.im/ffmpeg-video-player/devops">
    <img src="https://img.shields.io/gitter/room/rambodrahmani/ffmpeg-video-player.svg?logo=gitter" alt="Chat on Gitter">
</p>

![FFmpeg Video Player](/screenshots/2019-10-25-224809_1366x768_scrot.png)

# Prerequisites (Linux)
The project was developed on Arch Linux and all tutorials have been compiled and
executed only on Arch Linux. To get everything ready to compile the source
codes, just install FFMpeg:
```
sudo pacman -S ffmpeg
```
For the development version, install the ffmpeg-git package.
```
yay -S ffmpeg-git
```
There is also ffmpeg-full, which is built with as many optional features enabled
as possible.
```
yay -S ffmpeg-full
```

# Compilation
Each tutorial can be compiled manually using
```
$ gcc -o tutorial01 tutorial01.c -lavutil -lavformat -lavcodec -lswscale -lz -lm
```
You can also compile all the source files in this repo using the provided CMake
files using
```
$ cmake CMakeLists.txt -Bcmake-build-debug
$ cd cmake-build-debug/
$ make
```
As an example:
```
[rambodrahmani@rr-workstation ffmpeg-video-player]$ cmake CMakeLists.txt -Bcmake-build-debug
-- The C compiler identification is GNU 9.2.0
-- Check for working C compiler: /usr/bin/cc
-- Check for working C compiler: /usr/bin/cc -- works
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Detecting C compile features
-- Detecting C compile features - done
-- Found PkgConfig: /usr/bin/pkg-config (found version "1.6.3") 
-- Configuring done
-- Generating done
-- Build files have been written to: /home/rambodrahmani/DevOps/ffmpeg-video-player/cmake-build-debug
[rambodrahmani@rr-workstation ffmpeg-video-player]$ cd cmake-build-debug/
[rambodrahmani@rr-workstation cmake-build-debug]$ make
Scanning dependencies of target tutorial01-deprecated
[  2%] Building C object tutorial01/CMakeFiles/tutorial01-deprecated.dir/tutorial01-deprecated.c.o
/home/rambodrahmani/DevOps/ffmpeg-video-player/tutorial01/tutorial01-deprecated.c: In function ‘main’:

[...]

[ 94%] Linking C executable player-sdl
[ 94%] Built target player-sdl
Scanning dependencies of target player-sdl2
[ 97%] Building C object player/CMakeFiles/player-sdl2.dir/player-sdl2.c.o
[100%] Linking C executable player-sdl2
[100%] Built target player-sdl2

[rambodrahmani@rr-workstation cmake-build-debug]$ ./tutorial01/tutorial01
Invalid arguments.

Usage: ./tutorial01 <filename> <max-frames-to-decode>

e.g: ./tutorial01 /home/rambodrahmani/Videos/Labrinth-Jealous.mp4 200
[rambodrahmani@rr-workstation cmake-build-debug]$ cd ..
[rambodrahmani@rr-workstation ffmpeg-video-player]$ 
```
# Major opcode of failed request:  151 (GLX)
In case you end up having this error when trying to execute one of the
tutorials, then refer to the `Tearing` section below.

# Major opcode of failed request:  152 (GLX)
In case you end up having this error when trying to execute one of the
tutorials, then you are probabily have nvidia drivers installed on your system
with no NVIDIA Hardware. To check if this is the case, just run
```
pacman -Qs nvidia
```
if you find, among the other packages, the ```nvidia-340xx-utils``` and no
NVIDIA hardware in your machine, then you have to remove it
```
sudo pacman -R nvidia-340xx-utils
```
and reboot.

# Tearing
Starting from tutorial03 and noticed some screen tearing happening when playing
the media. To be precise vertical tearing.

At the time of this writing I am using Arch Linux and after making sure the
problem was not my code and some troubleshooting I managed to fix the tearing.

First of all use the following command to find out your graphic card:
```
[rambodrahmani@rr-workstation ~]$ lspci
00:00.0 Host bridge: Intel Corporation Intel Kaby Lake Host Bridge (rev 05)
00:01.0 PCI bridge: Intel Corporation Xeon E3-1200 v5/E3-1500 v5/6th Gen Core Processor PCIe Controller (x16) (rev 05)
00:02.0 VGA compatible controller: Intel Corporation HD Graphics 630 (rev 04)
00:14.0 USB controller: Intel Corporation 200 Series PCH USB 3.0 xHCI Controller
00:16.0 Communication controller: Intel Corporation 200 Series PCH CSME HECI #1
00:17.0 SATA controller: Intel Corporation 200 Series PCH SATA controller [AHCI mode]
00:1b.0 PCI bridge: Intel Corporation 200 Series PCH PCI Express Root Port #17 (rev f0)
00:1c.0 PCI bridge: Intel Corporation 200 Series PCH PCI Express Root Port #1 (rev f0)
00:1c.2 PCI bridge: Intel Corporation 200 Series PCH PCI Express Root Port #3 (rev f0)
00:1c.6 PCI bridge: Intel Corporation 200 Series PCH PCI Express Root Port #7 (rev f0)
00:1d.0 PCI bridge: Intel Corporation 200 Series PCH PCI Express Root Port #9 (rev f0)
00:1f.0 ISA bridge: Intel Corporation 200 Series PCH LPC Controller (Z270)
00:1f.2 Memory controller: Intel Corporation 200 Series PCH PMC
00:1f.3 Audio device: Intel Corporation 200 Series PCH HD Audio
00:1f.4 SMBus: Intel Corporation 200 Series PCH SMBus Controller
00:1f.6 Ethernet controller: Intel Corporation Ethernet Connection (2) I219-LM
01:00.0 PCI bridge: PLX Technology, Inc. PEX 8747 48-Lane, 5-Port PCI Express Gen 3 (8.0 GT/s) Switch (rev ca)
02:08.0 PCI bridge: PLX Technology, Inc. PEX 8747 48-Lane, 5-Port PCI Express Gen 3 (8.0 GT/s) Switch (rev ca)
02:10.0 PCI bridge: PLX Technology, Inc. PEX 8747 48-Lane, 5-Port PCI Express Gen 3 (8.0 GT/s) Switch (rev ca)
06:00.0 USB controller: ASMedia Technology Inc. Device 2142
07:00.0 Ethernet controller: Intel Corporation I210 Gigabit Network Connection (rev 03)
08:00.0 USB controller: ASMedia Technology Inc. Device 2142
```
As you can see I am using the 
```
Intel Corporation HD Graphics 630 (rev 04)
```
Often it is not recommended, however for the DDX driver (which provides 2D
acceleration in Xorg), install the xf86-video-intel package.

The Intel kernel module should load fine automatically on system boot. 

The SNA acceleration method causes tearing on some machines. To fix this, enable
the "TearFree" option in the driver by adding the following line to your
configuration file:
```
/etc/X11/xorg.conf.d/20-intel.conf
```
```
Section "Device"
 Identifier  "Intel Graphics"
 Driver      "intel"
 Option      "TearFree" "true"
EndSection
```
and reboot.

# An FFmpeg and SDL Tutorial

This repo contains an updated and reviewed version of the original
["An ffmpeg and SDL Tutorial or How to Write a Video Player in Less Than 1000 Lines"](http://dranger.com/ffmpeg/).

FFmpeg is a wonderful library for creating video applications or even general
purpose utilities. FFmpeg takes care of all the hard work of video processing by
doing all the decoding, encoding, muxing and demuxing for you. This can make
media applications much simpler to write. It's simple, written in C, fast, and
can decode almost any codec you'll find in use today, as well as encode several
other formats.

FFmpeg is a free software project, the product of which is a vast software suite
of libraries and programs for handling video, audio, and other multimedia files
and streams. At its core is the FFmpeg program itself, designed for
command-line-based processing of video and audio files, widely used for format
transcoding, basic editing (trimming and concatenation), video scaling, video
post-production effects, and standards compliance (SMPTE, ITU). FFmpeg includes
libavcodec, an audio/video codec library used by many commercial and free
software products, libavformat (Lavf), an audio/video container mux and demux
library, and the core ffmpeg command line program for transcoding multimedia
files. FFmpeg is published under the GNU Lesser General Public License 2.1+ or
GNU General Public License 2+ (depending on which options are enabled).
The name of the project is inspired by the MPEG video standards group, together
with "FF" for "fast forward". The logo uses a zigzag pattern that shows how MPEG
video codecs handle entropy encoding.
FFmpeg is part of the workflow of hundreds of other software projects, and its
libraries are a core part of software media players such as VLC, and has been
included in core processing for YouTube and the iTunes inventory of files.
Codecs for the encoding and/or decoding of most of all known audio and video
file formats is included, making it highly useful for the transcoding of common
and uncommon media files into a single common format.

The only problem is that documentation is basically nonexistent. There is a
single tutorial that shows the basics of FFmpeg and auto-generated doxygen
documents. That's it. So, when I decided to learn about FFmpeg, and in the
process about how digital video and audio applications work, I decided to
document the process and present it as a tutorial.

There is a sample program that comes with ffmpeg called ffplay. It is a simple C
program that implements a complete video player using FFmpeg. This tutorial will
begin with an updated version of the original tutorial, written by Martin Böhme 
(I have stolen liberally borrowed from that work), and work from there to
developing a working video player, based on Fabrice Bellard's ffplay.c. In each
tutorial, I'll introduce a new idea (or two) and explain how we implement it.
Each tutorial will have a C file so you can download it, compile it, and follow
along at home. The source files will show you how the real program works, how we
move all the pieces around, as well as showing you the technical details that
are unimportant to the tutorial. By the time we are finished, we will have a
working video player written in less than 1000 lines of code!

In making the player, we will be using SDL to output the audio and video of the
media file. SDL is an excellent cross-platform multimedia library that's used in
MPEG playback software, emulators, and many video games. You will need to
download and install the SDL development libraries for your system in order to
compile the programs in this tutorial.

This tutorial is meant for people with a decent programming background. At the
very least you should know C and have some idea about concepts like queues,
mutexes, and so on. You should know some basics about multimedia; things like
waveforms and such, but you don't need to know a lot, as I explain a lot of
those concepts in this tutorial.

##### Originally seen at: http://dranger.com/ffmpeg/
##### This repo contains both the original (deprecated) and updated implementations for each tutorial.
##### The source codes originally written by Martin Bohme are also provided for ease of access.

--

Rambod Rahmani <<rambodrahmani@autistici.org>>

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

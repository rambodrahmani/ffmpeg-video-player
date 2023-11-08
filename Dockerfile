FROM ubuntu:22.04

# upgrade existing stuff
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get upgrade -y

# install packages for development
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y   \
    software-properties-common build-essential gcc g++  \
    gdb clang cmake rsync apt-utils sudo vim zlib1g-dev \
    libncurses5-dev libgdbm-dev libnss3-dev libssl-dev  \
    libreadline-dev libffi-dev wget zip unzip

# packages needed to build the project
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y   \
    ffmpeg libavcodec-dev libavformat-dev libavutil-dev \
    libavfilter-dev libswscale-dev libsdl-image1.2-dev  \
    libsdl1.2-dev libsdl2-dev libavdevice-dev

# packages needed when container wants to use host S server
RUN apt-get install -y xdg-user-dirs xdg-utils

# create non-root user ffmpeg
RUN adduser --disabled-password --gecos '' ffmpeg
RUN adduser ffmpeg sudo
RUN echo '%sudo ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers
USER ffmpeg
WORKDIR /home/ffmpeg
RUN echo "export USER=ffmpeg" >> ~/.bashrc
ENTRYPOINT /bin/bash
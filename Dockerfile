# syntax=docker/dockerfile:1.7
FROM ubuntu:22.04 AS toolchain

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential ca-certificates cmake pkg-config python3-pip \
      libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
      libgstreamer-plugins-bad1.0-dev gstreamer1.0-tools \
      gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
      gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly \
      gstreamer1.0-libav gstreamer1.0-nice \
    && rm -rf /var/lib/apt/lists/*
RUN pip3 install --no-cache-dir mojo==1.0.0

FROM toolchain AS builder
WORKDIR /src
COPY . .
RUN cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --parallel \
    && ctest --test-dir build --output-on-failure \
    && LD_LIBRARY_PATH=/src/build mojo run -I . tests/mojo_smoke.mojo \
    && mojo precompile gstreamer_mojo -o build/gstreamer_mojo.mojoc \
    && mojo build -I . examples/video_frames.mojo -o build/video-frames \
    && mojo build -I . examples/pcm_to_opus.mojo -o build/pcm-to-opus \
    && mojo build -I . examples/h264_decode.mojo -o build/h264-decode \
    && mojo build -I . examples/webrtc_offer.mojo -o build/webrtc-offer

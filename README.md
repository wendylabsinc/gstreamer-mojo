# gstreamer-mojo

[![Continuous integration](https://github.com/wendylabsinc/gstreamer-mojo/actions/workflows/ci.yml/badge.svg)](https://github.com/wendylabsinc/gstreamer-mojo/actions/workflows/ci.yml)
[![Mojo 1.0+](https://img.shields.io/badge/Mojo-1.0%2B-FF4C1D)](https://docs.modular.com/mojo/)
[![macOS and Linux](https://img.shields.io/badge/platform-macOS%20%7C%20Linux-171C23)](#platform-support)
[![License: BSD-3-Clause](https://img.shields.io/badge/License-BSD--3--Clause-171C23.svg)](LICENSE)

Native GStreamer bindings for Mojo 1.0 and higher, maintained by Wendy Labs.
The package uses a small, versioned C ABI so GObject ownership and GStreamer’s
unstable WebRTC C API never leak into Mojo application code.

## Quickstart

Install Mojo 1.0+, CMake, `pkg-config`, and GStreamer 1.20+.

```bash
uv pip install mojo==1.0.0
```

macOS (Apple silicon):

```bash
brew install cmake pkgconf gstreamer libnice
```

Ubuntu 22.04+:

```bash
sudo apt-get install cmake pkg-config libgstreamer1.0-dev \
  libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev \
  gstreamer1.0-plugins-{base,good,bad,ugly} gstreamer1.0-libav gstreamer1.0-nice
```

Build the bridge and run the first Mojo program:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
export LD_LIBRARY_PATH="$PWD/build:${LD_LIBRARY_PATH:-}"       # Linux
export DYLD_LIBRARY_PATH="$PWD/build:${DYLD_LIBRARY_PATH:-}"   # macOS
mojo run -I . examples/video_frames.mojo
```

The core API is deliberately small:

```mojo
from gstreamer_mojo import Pipeline, STATE_PLAYING

def main() raises:
    var camera = Pipeline(
        "autovideosrc ! videoconvert ! video/x-raw,format=RGB ! "
        "appsink name=sink sync=false max-buffers=2 drop=true"
    )
    _ = camera.set_state(STATE_PLAYING)
    var next = camera.pull_frame(timeout_ns=1_000_000_000)
    if next is not None:
        var frame = next.take()
        print(frame.width, frame.height, frame.format, len(frame.data))
```

## Features

- Owned pipeline lifecycle: parse, play, pause, stop, seek, property updates,
  EOS, warnings, and actionable bus errors.
- `appsink` frame extraction with owned bytes, caps-derived dimensions and
  format, timestamps, stride, bounded buffers, and drop accounting.
- `appsrc` caps, timestamped buffer injection, backpressure through normal
  GStreamer properties, and end-of-stream.
- H.264 decode through platform GStreamer decoders with RGB frame delivery; see
  [`examples/h264_decode.mojo`](examples/h264_decode.mojo).
- Browser-style 48 kHz S16LE PCM injection and outbound Opus encoding; see
  [`examples/pcm_to_opus.mojo`](examples/pcm_to_opus.mojo).
- `webrtcbin` offers, answers, local/remote descriptions, trickle ICE, text and
  binary data channels, and bounded receive queues.
- Typed pipeline state, WebRTC connection state, frame rate, received/dropped
  frames, bytes, last-frame time, reconnect count, and last error.
- Runtime plugin discovery and GStreamer version reporting.
- A versioned, documented C ABI usable by other native languages as well.

`gstreamer-mojo` owns the media transport. Vendor signaling belongs in the
calling package: for example, `unitree-mojo` can implement LocalSTA HTTP/AES
signaling and send Unitree camera or megaphone commands through this package’s
SDP, ICE, and data-channel APIs. This separation keeps credentials, retry
policy, and robot-specific wire messages out of the general media library.

## WebRTC flow

Create a `webrtcbin`, move the pipeline to `READY`, optionally create a data
channel, and call `create_offer()`. Send that SDP through your signaling
service, install the answer with `set_remote_description()`, and exchange
trickle ICE using `pop_ice_candidate()` and `add_ice_candidate()`. A send made
before the data channel reaches `OPEN` raises an error instead of entering an
invalid GStreamer state.

See [`examples/webrtc_offer.mojo`](examples/webrtc_offer.mojo).

## Tests

```bash
make test
docker build --target builder -t gstreamer-mojo-test .
```

The native integration suite currently exercises more than 100 assertions
against real GStreamer pipelines: lifecycle transitions, properties, plugin
discovery, RGB frames, undersized buffers, health counters, PCM round trips,
Opus encode/decode, H.264 encode/decode, EOS, bus failures, WebRTC SDP, ICE,
data-channel guards, ABI compatibility, and invalid inputs. A Mojo integration
program verifies dynamic loading, ownership, frame decoding, and typed health.

CI runs the suite on Linux `x86_64`, Linux `aarch64`, and macOS Apple silicon,
then precompiles the Mojo package and every non-file-dependent example.

## Platform support

| Platform | Architecture | Status |
| --- | --- | --- |
| Linux 22.04+ | `x86_64` | CI tested |
| Linux 22.04+ | `aarch64` | CI tested |
| macOS 15+ | Apple silicon | CI tested |
| Windows | — | Planned; tracked in GitHub Issues |

Mojo itself requires Apple silicon on macOS. The C bridge uses standard
GStreamer APIs available on other architectures, but the support promise follows
the Mojo runtime.

## ABI and ownership

The ABI version is returned by `gstreamer_mojo_abi_version()`. Mojo checks it
before creating a pipeline. Buffers crossing the boundary are copied into
caller-owned memory; no `GstBuffer`, `GstSample`, `GBytes`, `GObject`, callback,
or exception crosses the boundary. Destroying `Pipeline` stops the pipeline and
releases the bus, data channel, queues, and all GStreamer objects.

## License

BSD-3-Clause.

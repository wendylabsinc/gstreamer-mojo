from gstreamer_mojo import Pipeline, STATE_PLAYING


def main() raises:
    var pipeline = Pipeline(
        "appsrc name=source is-live=true format=time block=true "
        "caps=audio/x-raw,format=S16LE,channels=1,rate=48000,layout=interleaved ! "
        "audioconvert ! opusenc frame-size=20 bitrate=32000 ! "
        "appsink name=sink sync=false"
    )
    _ = pipeline.set_state(STATE_PLAYING)

    # One 20 ms, 48 kHz, mono S16LE silence frame. Replace this with browser
    # microphone PCM delivered by your application transport.
    var pcm = List[UInt8](unsafe_uninit_length=1920)
    for index in range(len(pcm)):
        pcm[index] = 0
    pipeline.push(pcm, pts_ns=0, duration_ns=20_000_000)

    var packet = pipeline.pull_frame(timeout_ns=2_000_000_000, max_bytes=4096)
    if packet is not None:
        print("encoded Opus packet:", len(packet.take().data), "bytes")
    pipeline.end()

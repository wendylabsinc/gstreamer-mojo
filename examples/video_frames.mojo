from gstreamer_mojo import Pipeline, STATE_PLAYING


def main() raises:
    var pipeline = Pipeline(
        "videotestsrc num-buffers=30 ! "
        "video/x-raw,format=RGB,width=640,height=360,framerate=30/1 ! "
        "appsink name=sink sync=false max-buffers=2 drop=true"
    )
    _ = pipeline.set_state(STATE_PLAYING)

    while True:
        var frame = pipeline.pull_frame(
            timeout_ns=1_000_000_000, max_bytes=640 * 360 * 3
        )
        if frame is None:
            break
        var pixels = frame.take()
        print(
            "frame",
            pixels.width,
            "x",
            pixels.height,
            pixels.format,
            len(pixels.data),
            "bytes",
        )

    var health = pipeline.health()
    print("received", health.frames_received, "frames at", health.frame_rate, "fps")

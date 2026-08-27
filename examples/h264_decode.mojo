from gstreamer_mojo import Pipeline, STATE_PLAYING


def main() raises:
    # `source.h264` must be an Annex B H.264 elementary stream. This pipeline
    # uses GStreamer's platform-selected decoder and returns owned RGB frames.
    var pipeline = Pipeline(
        "filesrc location=source.h264 ! h264parse ! decodebin ! "
        "videoconvert ! video/x-raw,format=RGB ! "
        "appsink name=sink sync=false max-buffers=2 drop=true"
    )
    _ = pipeline.set_state(STATE_PLAYING)
    while True:
        var frame = pipeline.pull_frame(timeout_ns=1_000_000_000)
        if frame is None:
            break
        var image = frame.take()
        print(image.width, "x", image.height, image.format, len(image.data))

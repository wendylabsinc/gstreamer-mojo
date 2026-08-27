from gstreamer_mojo import (
    Pipeline,
    STATE_NULL,
    STATE_PLAYING,
    gstreamer_version,
    has_element,
)


def require(condition: Bool, message: String) raises:
    if not condition:
        raise Error(message)


def main() raises:
    var library = "build/libgstreamer_mojo.so"
    require("GStreamer" in gstreamer_version(library), "runtime version")
    require(has_element("fakesrc", library), "fakesrc plugin")
    require(not has_element("definitely-not-a-plugin", library), "missing plugin")

    var pipeline = Pipeline(
        "videotestsrc num-buffers=3 ! "
        "video/x-raw,format=RGB,width=16,height=8,framerate=30/1 ! "
        "appsink name=sink sync=false",
        library,
    )
    require(pipeline.set_state(STATE_PLAYING), "playing synchronously")
    var frame = pipeline.pull_frame(timeout_ns=2_000_000_000, max_bytes=4096)
    require(frame is not None, "frame available")
    var value = frame.take()
    require(value.width == 16, "frame width")
    require(value.height == 8, "frame height")
    require(value.format == "RGB", "frame format")
    require(len(value.data) == 384, "frame byte count")
    var health = pipeline.health()
    require(health.frames_received == 1, "health frame count")
    pipeline.note_reconnect()
    require(pipeline.health().reconnect_count == 1, "health reconnect count")
    pipeline.stop()
    require(pipeline.state().value == STATE_NULL.value, "pipeline stopped")
    print("Mojo integration smoke passed")

comptime NANOSECONDS_PER_SECOND = UInt64(1_000_000_000)
comptime NO_TIMESTAMP = UInt64(18_446_744_073_709_551_615)


struct PipelineState(Copyable, Movable, ImplicitlyCopyable):
    """A typed GStreamer pipeline state."""

    var value: Int32

    def __init__(out self, value: Int32):
        self.value = value

    def __str__(self) -> String:
        if self.value == 1:
            return "null"
        if self.value == 2:
            return "ready"
        if self.value == 3:
            return "paused"
        if self.value == 4:
            return "playing"
        return "void-pending"


comptime STATE_NULL = PipelineState(1)
comptime STATE_READY = PipelineState(2)
comptime STATE_PAUSED = PipelineState(3)
comptime STATE_PLAYING = PipelineState(4)


struct ConnectionState(Copyable, Movable, ImplicitlyCopyable):
    """A typed WebRTC peer-connection state."""

    var value: UInt32

    def __init__(out self, value: UInt32):
        self.value = value

    def __str__(self) -> String:
        if self.value == 0:
            return "new"
        if self.value == 1:
            return "connecting"
        if self.value == 2:
            return "connected"
        if self.value == 3:
            return "disconnected"
        if self.value == 4:
            return "failed"
        if self.value == 5:
            return "closed"
        return "unavailable"


struct Frame(Movable):
    """An owned frame copied from an `appsink` sample."""

    var data: List[UInt8]
    var width: UInt32
    var height: UInt32
    var stride: UInt32
    var pts_ns: UInt64
    var duration_ns: UInt64
    var format: String

    def __init__(
        out self,
        data: List[UInt8],
        width: UInt32,
        height: UInt32,
        stride: UInt32,
        pts_ns: UInt64,
        duration_ns: UInt64,
        format: String,
    ):
        self.data = List[UInt8]()
        for byte in data:
            self.data.append(byte)
        self.width = width
        self.height = height
        self.stride = stride
        self.pts_ns = pts_ns
        self.duration_ns = duration_ns
        self.format = format


struct IceCandidate(Movable):
    """A trickle ICE candidate emitted by `webrtcbin`."""

    var mline_index: UInt32
    var candidate: String

    def __init__(out self, mline_index: UInt32, candidate: String):
        self.mline_index = mline_index
        self.candidate = candidate


struct DataChannelMessage(Movable):
    """An owned text or binary WebRTC data-channel message."""

    var data: List[UInt8]
    var is_text: Bool

    def __init__(out self, data: List[UInt8], is_text: Bool):
        self.data = List[UInt8]()
        for byte in data:
            self.data.append(byte)
        self.is_text = is_text

    def text(self) raises -> String:
        if not self.is_text:
            raise Error("WebRTC data-channel message is binary")
        return String(from_utf8=self.data)


struct PipelineHealth(Copyable, Movable):
    """A point-in-time pipeline and media health snapshot."""

    var pipeline_state: PipelineState
    var connection_state: ConnectionState
    var reconnect_count: UInt32
    var frames_received: UInt64
    var frames_dropped: UInt64
    var bytes_received: UInt64
    var last_frame_monotonic_ns: UInt64
    var frame_rate: Float64

    def __init__(
        out self,
        pipeline_state: PipelineState,
        connection_state: ConnectionState,
        reconnect_count: UInt32,
        frames_received: UInt64,
        frames_dropped: UInt64,
        bytes_received: UInt64,
        last_frame_monotonic_ns: UInt64,
        frame_rate: Float64,
    ):
        self.pipeline_state = pipeline_state
        self.connection_state = connection_state
        self.reconnect_count = reconnect_count
        self.frames_received = frames_received
        self.frames_dropped = frames_dropped
        self.bytes_received = bytes_received
        self.last_frame_monotonic_ns = last_frame_monotonic_ns
        self.frame_rate = frame_rate

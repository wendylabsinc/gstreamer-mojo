from std.ffi import OwnedDLHandle, c_int, c_size_t

from .types import (
    ConnectionState,
    DataChannelMessage,
    Frame,
    IceCandidate,
    PipelineHealth,
    PipelineState,
    STATE_NULL,
)


comptime ABI_VERSION = UInt32(1)
comptime DEFAULT_LIBRARY = "libgstreamer_mojo.so"
comptime RESULT_AGAIN = 1
comptime RESULT_EOS = 2
comptime ERROR_BUFFER_TOO_SMALL = -4


def _string_from_buffer(buffer: List[UInt8], count: Int) raises -> String:
    var bytes = List[UInt8]()
    for index in range(count):
        bytes.append(buffer[index])
    return String(from_utf8=bytes)


struct Pipeline(Movable):
    """Owned GStreamer pipeline with app and WebRTC operations."""

    var _library: OwnedDLHandle
    var _handle: OpaquePointer[MutUntrackedOrigin]

    def __init__(
        out self,
        description: String,
        library_path: String = DEFAULT_LIBRARY,
    ) raises:
        self._library = OwnedDLHandle(library_path)
        var abi = self._library.get_function[UInt32](
            "gstreamer_mojo_abi_version"
        )
        if abi() != ABI_VERSION:
            raise Error("gstreamer-mojo C ABI version mismatch")
        var create = self._library.get_function[
            OpaquePointer[MutUntrackedOrigin]
        ]("gstreamer_mojo_pipeline_create")
        var pipeline_description = description
        self._handle = create(
            pipeline_description.as_c_string_slice().unsafe_ptr()
        )
        if Int(self._handle) == 0:
            raise Error("GStreamer pipeline creation failed")

    def __deinit__(deinit self):
        if Int(self._handle) != 0:
            try:
                var destroy = self._library.get_function[NoneType](
                    "gstreamer_mojo_pipeline_destroy"
                )
                destroy(self._handle)
            except:
                pass

    def _check(ref self, operation: String, result: c_int) raises:
        if result < 0:
            raise Error(operation + " failed: " + self.last_error())

    def set_state(
        ref self, state: PipelineState, timeout_ns: UInt64 = 5_000_000_000
    ) raises -> Bool:
        var call = self._library.get_function[c_int](
            "gstreamer_mojo_pipeline_set_state"
        )
        var result = call(self._handle, state.value, timeout_ns)
        self._check("pipeline state change", result)
        return result == 0

    def state(ref self) raises -> PipelineState:
        var call = self._library.get_function[c_int](
            "gstreamer_mojo_pipeline_get_state"
        )
        var result = call(self._handle)
        self._check("reading pipeline state", result)
        return PipelineState(result)

    def stop(ref self) raises:
        _ = self.set_state(STATE_NULL)

    def poll_bus(ref self, timeout_ns: UInt64 = 0) raises -> Int:
        """Return 0 for a warning, 1 when idle, and 2 for end-of-stream."""
        var call = self._library.get_function[c_int](
            "gstreamer_mojo_pipeline_poll_bus"
        )
        var result = call(self._handle, timeout_ns)
        self._check("polling the pipeline bus", result)
        return Int(result)

    def seek(ref self, position_ns: Int64) raises:
        var call = self._library.get_function[c_int](
            "gstreamer_mojo_pipeline_seek"
        )
        self._check("pipeline seek", call(self._handle, position_ns))

    def set_string(
        ref self, element: String, property: String, value: String
    ) raises:
        var call = self._library.get_function[c_int](
            "gstreamer_mojo_pipeline_set_string"
        )
        var e = element
        var p = property
        var v = value
        self._check(
            "setting string property",
            call(
                self._handle,
                e.as_c_string_slice().unsafe_ptr(),
                p.as_c_string_slice().unsafe_ptr(),
                v.as_c_string_slice().unsafe_ptr(),
            ),
        )

    def set_int(
        ref self, element: String, property: String, value: Int64
    ) raises:
        var call = self._library.get_function[c_int](
            "gstreamer_mojo_pipeline_set_int"
        )
        var e = element
        var p = property
        self._check(
            "setting integer property",
            call(
                self._handle,
                e.as_c_string_slice().unsafe_ptr(),
                p.as_c_string_slice().unsafe_ptr(),
                value,
            ),
        )

    def set_bool(
        ref self, element: String, property: String, value: Bool
    ) raises:
        var call = self._library.get_function[c_int](
            "gstreamer_mojo_pipeline_set_bool"
        )
        var e = element
        var p = property
        self._check(
            "setting Boolean property",
            call(
                self._handle,
                e.as_c_string_slice().unsafe_ptr(),
                p.as_c_string_slice().unsafe_ptr(),
                1 if value else 0,
            ),
        )

    def pull_frame(
        ref self,
        appsink: String = "sink",
        timeout_ns: UInt64 = 0,
        max_bytes: Int = 16_777_216,
    ) raises -> Optional[Frame]:
        var data = List[UInt8](unsafe_uninit_length=max_bytes)
        # gm_frame_info is 56 bytes with 8-byte alignment.
        var metadata = List[UInt8](unsafe_uninit_length=56)
        var call = self._library.get_function[c_int](
            "gstreamer_mojo_appsink_pull"
        )
        var name = appsink
        var result = call(
            self._handle,
            name.as_c_string_slice().unsafe_ptr(),
            timeout_ns,
            data.unsafe_ptr(),
            c_size_t(max_bytes),
            metadata.unsafe_ptr(),
        )
        if result == RESULT_AGAIN:
            return None
        if result == ERROR_BUFFER_TOO_SMALL:
            var size = metadata.unsafe_ptr().unsafe_bitcast[UInt32]()[unsafe_offset=4]
            raise Error("appsink frame exceeds max_bytes: " + String(size))
        self._check("pulling appsink frame", result)
        var words = metadata.unsafe_ptr().unsafe_bitcast[UInt32]()
        if words[unsafe_offset=0] != ABI_VERSION:
            raise Error("gstreamer-mojo frame ABI version mismatch")
        var size = Int(words[unsafe_offset=4])
        var owned = List[UInt8]()
        for index in range(size):
            owned.append(data[index])
        var timestamps = metadata.unsafe_ptr().unsafe_bitcast[UInt64]()
        var format_bytes = List[UInt8]()
        for index in range(16):
            var byte = metadata[40 + index]
            if byte == 0:
                break
            format_bytes.append(byte)
        return Frame(
            data=owned,
            width=words[unsafe_offset=1],
            height=words[unsafe_offset=2],
            stride=words[unsafe_offset=3],
            pts_ns=timestamps[unsafe_offset=3],
            duration_ns=timestamps[unsafe_offset=4],
            format=String(from_utf8=format_bytes),
        )

    def set_appsrc_caps(
        ref self, caps: String, appsrc: String = "source"
    ) raises:
        var call = self._library.get_function[c_int](
            "gstreamer_mojo_appsrc_set_caps"
        )
        var name = appsrc
        var value = caps
        self._check(
            "setting appsrc caps",
            call(
                self._handle,
                name.as_c_string_slice().unsafe_ptr(),
                value.as_c_string_slice().unsafe_ptr(),
            ),
        )

    def push(
        ref self,
        data: List[UInt8],
        appsrc: String = "source",
        pts_ns: UInt64 = 18_446_744_073_709_551_615,
        duration_ns: UInt64 = 18_446_744_073_709_551_615,
    ) raises:
        var call = self._library.get_function[c_int](
            "gstreamer_mojo_appsrc_push"
        )
        var name = appsrc
        self._check(
            "pushing appsrc buffer",
            call(
                self._handle,
                name.as_c_string_slice().unsafe_ptr(),
                data.unsafe_ptr(),
                c_size_t(len(data)),
                pts_ns,
                duration_ns,
            ),
        )

    def end(ref self, appsrc: String = "source") raises:
        var call = self._library.get_function[c_int](
            "gstreamer_mojo_appsrc_end"
        )
        var name = appsrc
        self._check(
            "ending appsrc",
            call(self._handle, name.as_c_string_slice().unsafe_ptr()),
        )

    def create_offer(
        ref self, webrtc: String = "webrtc", max_bytes: Int = 65_536
    ) raises -> String:
        return self._create_sdp(
            "gstreamer_mojo_webrtc_create_offer", webrtc, max_bytes
        )

    def create_answer(
        ref self, webrtc: String = "webrtc", max_bytes: Int = 65_536
    ) raises -> String:
        return self._create_sdp(
            "gstreamer_mojo_webrtc_create_answer", webrtc, max_bytes
        )

    def _create_sdp(
        ref self, symbol: String, webrtc: String, max_bytes: Int
    ) raises -> String:
        var output = List[UInt8](unsafe_uninit_length=max_bytes)
        var call = self._library.get_function[c_int](symbol)
        var name = webrtc
        var result = call(
            self._handle,
            name.as_c_string_slice().unsafe_ptr(),
            output.unsafe_ptr(),
            c_size_t(max_bytes),
        )
        self._check("creating WebRTC SDP", result)
        return _string_from_buffer(output, Int(result))

    def set_remote_description(
        ref self, kind: String, sdp: String, webrtc: String = "webrtc"
    ) raises:
        var call = self._library.get_function[c_int](
            "gstreamer_mojo_webrtc_set_remote_description"
        )
        var name = webrtc
        var k = kind
        var value = sdp
        self._check(
            "setting remote WebRTC description",
            call(
                self._handle,
                name.as_c_string_slice().unsafe_ptr(),
                k.as_c_string_slice().unsafe_ptr(),
                value.as_c_string_slice().unsafe_ptr(),
            ),
        )

    def add_ice_candidate(
        ref self,
        mline_index: UInt32,
        candidate: String,
        webrtc: String = "webrtc",
    ) raises:
        var call = self._library.get_function[c_int](
            "gstreamer_mojo_webrtc_add_ice_candidate"
        )
        var name = webrtc
        var value = candidate
        self._check(
            "adding WebRTC ICE candidate",
            call(
                self._handle,
                name.as_c_string_slice().unsafe_ptr(),
                mline_index,
                value.as_c_string_slice().unsafe_ptr(),
            ),
        )

    def pop_ice_candidate(
        ref self, max_bytes: Int = 4096
    ) raises -> Optional[IceCandidate]:
        var output = List[UInt8](unsafe_uninit_length=max_bytes)
        var mline = UInt32(0)
        var call = self._library.get_function[c_int](
            "gstreamer_mojo_webrtc_pop_ice_candidate"
        )
        var result = call(
            self._handle,
            Pointer(to=mline),
            output.unsafe_ptr(),
            c_size_t(max_bytes),
        )
        if result == RESULT_AGAIN:
            return None
        self._check("reading WebRTC ICE candidate", result)
        return IceCandidate(mline, _string_from_buffer(output, Int(result)))

    def create_data_channel(
        ref self, label: String, webrtc: String = "webrtc"
    ) raises:
        var call = self._library.get_function[c_int](
            "gstreamer_mojo_webrtc_create_data_channel"
        )
        var name = webrtc
        var channel_label = label
        self._check(
            "creating WebRTC data channel",
            call(
                self._handle,
                name.as_c_string_slice().unsafe_ptr(),
                channel_label.as_c_string_slice().unsafe_ptr(),
            ),
        )

    def send_text(ref self, text: String) raises:
        var call = self._library.get_function[c_int](
            "gstreamer_mojo_webrtc_send_text"
        )
        var value = text
        self._check(
            "sending WebRTC text",
            call(self._handle, value.as_c_string_slice().unsafe_ptr()),
        )

    def send_binary(ref self, data: List[UInt8]) raises:
        var call = self._library.get_function[c_int](
            "gstreamer_mojo_webrtc_send_binary"
        )
        self._check(
            "sending WebRTC binary data",
            call(self._handle, data.unsafe_ptr(), c_size_t(len(data))),
        )

    def pop_message(
        ref self, max_bytes: Int = 1_048_576
    ) raises -> Optional[DataChannelMessage]:
        var output = List[UInt8](unsafe_uninit_length=max_bytes)
        var is_text = Int32(0)
        var call = self._library.get_function[c_int](
            "gstreamer_mojo_webrtc_pop_message"
        )
        var result = call(
            self._handle,
            output.unsafe_ptr(),
            c_size_t(max_bytes),
            Pointer(to=is_text),
        )
        if result == RESULT_AGAIN:
            return None
        self._check("reading WebRTC data-channel message", result)
        var owned = List[UInt8]()
        for index in range(Int(result)):
            owned.append(output[index])
        return DataChannelMessage(owned, is_text == 1)

    def note_reconnect(ref self) raises:
        var call = self._library.get_function[NoneType](
            "gstreamer_mojo_pipeline_note_reconnect"
        )
        call(self._handle)

    def health(ref self) raises -> PipelineHealth:
        # gm_health is 56 bytes with 8-byte alignment.
        var storage = List[UInt8](unsafe_uninit_length=56)
        var call = self._library.get_function[c_int](
            "gstreamer_mojo_pipeline_health"
        )
        self._check("reading pipeline health", call(self._handle, storage.unsafe_ptr()))
        var words = storage.unsafe_ptr().unsafe_bitcast[UInt32]()
        if words[unsafe_offset=0] != ABI_VERSION:
            raise Error("gstreamer-mojo health ABI version mismatch")
        var counters = storage.unsafe_ptr().unsafe_bitcast[UInt64]()
        var floating = storage.unsafe_ptr().unsafe_bitcast[Float64]()
        return PipelineHealth(
            pipeline_state=PipelineState(Int32(words[unsafe_offset=1])),
            connection_state=ConnectionState(words[unsafe_offset=2]),
            reconnect_count=words[unsafe_offset=3],
            frames_received=counters[unsafe_offset=2],
            frames_dropped=counters[unsafe_offset=3],
            bytes_received=counters[unsafe_offset=4],
            last_frame_monotonic_ns=counters[unsafe_offset=5],
            frame_rate=floating[unsafe_offset=6],
        )

    def last_error(ref self) raises -> String:
        var output = List[UInt8](unsafe_uninit_length=4096)
        var call = self._library.get_function[c_int](
            "gstreamer_mojo_last_error"
        )
        var count = call(self._handle, output.unsafe_ptr(), c_size_t(4096))
        if count < 0:
            return "unknown GStreamer error"
        return _string_from_buffer(output, Int(count))

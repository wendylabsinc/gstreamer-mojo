from std.ffi import OwnedDLHandle, c_int, c_size_t

from .pipeline import ABI_VERSION, DEFAULT_LIBRARY


def gstreamer_version(
    library_path: String = DEFAULT_LIBRARY,
) raises -> String:
    """Return the loaded GStreamer runtime version."""
    var library = OwnedDLHandle(library_path)
    var abi = library.get_function[UInt32]("gstreamer_mojo_abi_version")
    if abi() != ABI_VERSION:
        raise Error("gstreamer-mojo C ABI version mismatch")
    var output = List[UInt8](unsafe_uninit_length=256)
    var call = library.get_function[c_int](
        "gstreamer_mojo_copy_gstreamer_version"
    )
    var count = call(output.unsafe_ptr(), c_size_t(256))
    if count < 0:
        raise Error("reading GStreamer runtime version failed")
    var bytes = List[UInt8]()
    for index in range(Int(count)):
        bytes.append(output[index])
    return String(from_utf8=bytes)


def has_element(
    factory_name: String, library_path: String = DEFAULT_LIBRARY
) raises -> Bool:
    """Return whether a GStreamer element factory is installed."""
    var library = OwnedDLHandle(library_path)
    var call = library.get_function[c_int]("gstreamer_mojo_has_element")
    var name = factory_name
    return call(name.as_c_string_slice().unsafe_ptr()) == 1

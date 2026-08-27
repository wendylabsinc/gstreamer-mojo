from gstreamer_mojo import Pipeline, STATE_READY


def main() raises:
    var peer = Pipeline("webrtcbin name=webrtc bundle-policy=max-bundle")
    _ = peer.set_state(STATE_READY)
    peer.create_data_channel("commands")
    print(peer.create_offer())

    # Send the SDP through your signaling service, call
    # set_remote_description("answer", answer), then exchange candidates with
    # pop_ice_candidate() and add_ice_candidate().

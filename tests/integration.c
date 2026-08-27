#include "gstreamer_mojo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NS_PER_SECOND UINT64_C(1000000000)

static int assertions = 0;
static int failures = 0;

#define CHECK(condition)                                                        \
  do {                                                                          \
    assertions++;                                                               \
    if (!(condition)) {                                                         \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);      \
      failures++;                                                               \
    }                                                                           \
  } while (0)

static void check_runtime(void) {
  CHECK(gstreamer_mojo_abi_version() == 1);
  CHECK(strstr(gstreamer_mojo_gstreamer_version(), "GStreamer") != NULL);
  char version[128];
  CHECK(gstreamer_mojo_copy_gstreamer_version(version, sizeof(version)) > 0);
  CHECK(strstr(version, "GStreamer") != NULL);
  CHECK(gstreamer_mojo_copy_gstreamer_version(version, 1) ==
        GM_ERROR_BUFFER_TOO_SMALL);
  CHECK(gstreamer_mojo_has_element("fakesrc") == 1);
  CHECK(gstreamer_mojo_has_element("appsink") == 1);
  CHECK(gstreamer_mojo_has_element("appsrc") == 1);
  CHECK(gstreamer_mojo_has_element("webrtcbin") == 1);
  CHECK(gstreamer_mojo_has_element("not-a-real-element") == 0);
  CHECK(gstreamer_mojo_has_element(NULL) == 0);
}

static void check_arguments(void) {
  CHECK(gstreamer_mojo_pipeline_create(NULL) == NULL);
  CHECK(gstreamer_mojo_pipeline_create("") == NULL);
  CHECK(gstreamer_mojo_pipeline_get_state(NULL) == GM_ERROR_ARGUMENT);
  CHECK(gstreamer_mojo_pipeline_set_state(NULL, GM_STATE_PLAYING, 0) ==
        GM_ERROR_ARGUMENT);
  CHECK(gstreamer_mojo_pipeline_poll_bus(NULL, 0) == GM_ERROR_ARGUMENT);
  CHECK(gstreamer_mojo_pipeline_seek(NULL, 0) == GM_ERROR_ARGUMENT);
  CHECK(gstreamer_mojo_pipeline_health(NULL, NULL) == GM_ERROR_ARGUMENT);
  gstreamer_mojo_pipeline_destroy(NULL);
  gstreamer_mojo_pipeline_note_reconnect(NULL);
}

static void check_lifecycle_and_properties(void) {
  gm_pipeline *pipeline = gstreamer_mojo_pipeline_create(
      "videotestsrc name=source num-buffers=1 ! identity name=filter ! "
      "fakesink name=sink sync=false");
  CHECK(pipeline != NULL);
  CHECK(gstreamer_mojo_pipeline_get_state(pipeline) == GM_STATE_NULL);
  CHECK(gstreamer_mojo_pipeline_set_bool(pipeline, "filter", "silent", 0) ==
        GM_OK);
  CHECK(gstreamer_mojo_pipeline_set_int(pipeline, "source", "num-buffers", 2) ==
        GM_OK);
  CHECK(gstreamer_mojo_pipeline_set_string(pipeline, "sink", "name",
                                            "renamed-sink") == GM_OK);
  CHECK(gstreamer_mojo_pipeline_set_bool(pipeline, "missing", "silent", 1) ==
        GM_ERROR_NOT_FOUND);
  CHECK(gstreamer_mojo_pipeline_set_bool(pipeline, "filter", "missing", 1) ==
        GM_ERROR_NOT_FOUND);
  char error[256];
  CHECK(gstreamer_mojo_last_error(pipeline, error, sizeof(error)) > 0);
  CHECK(strstr(error, "property") != NULL);
  CHECK(gstreamer_mojo_pipeline_set_state(pipeline, GM_STATE_READY,
                                           NS_PER_SECOND) == GM_OK);
  CHECK(gstreamer_mojo_pipeline_get_state(pipeline) == GM_STATE_READY);
  int state_result = gstreamer_mojo_pipeline_set_state(
      pipeline, GM_STATE_PLAYING, 5 * NS_PER_SECOND);
  CHECK(state_result == GM_OK || state_result == GM_AGAIN);
  CHECK(gstreamer_mojo_pipeline_set_state(pipeline, GM_STATE_NULL,
                                           NS_PER_SECOND) == GM_OK);
  CHECK(gstreamer_mojo_pipeline_get_state(pipeline) == GM_STATE_NULL);
  gstreamer_mojo_pipeline_destroy(pipeline);
}

static gm_pipeline *video_pipeline(int buffers) {
  char description[512];
  snprintf(description, sizeof(description),
           "videotestsrc num-buffers=%d ! "
           "video/x-raw,format=RGB,width=32,height=18,framerate=30/1 ! "
           "appsink name=sink sync=false max-buffers=8 drop=false",
           buffers);
  return gstreamer_mojo_pipeline_create(description);
}

static void check_appsink(void) {
  gm_pipeline *pipeline = video_pipeline(4);
  CHECK(pipeline != NULL);
  CHECK(gstreamer_mojo_pipeline_set_state(pipeline, GM_STATE_PLAYING,
                                           5 * NS_PER_SECOND) >= GM_OK);
  uint8_t bytes[4096];
  gm_frame_info frame;
  CHECK(gstreamer_mojo_appsink_pull(pipeline, "missing", NS_PER_SECOND, bytes,
                                    sizeof(bytes), &frame) == GM_ERROR_NOT_FOUND);
  int result = gstreamer_mojo_appsink_pull(
      pipeline, "sink", 2 * NS_PER_SECOND, bytes, 8, &frame);
  CHECK(result == GM_ERROR_BUFFER_TOO_SMALL);
  CHECK(frame.size == 32u * 18u * 3u);
  CHECK(frame.width == 32);
  CHECK(frame.height == 18);
  CHECK(frame.stride == 96);
  CHECK(strcmp(frame.format, "RGB") == 0);
  for (int i = 0; i < 3; i++) {
    result = gstreamer_mojo_appsink_pull(pipeline, "sink", 2 * NS_PER_SECOND,
                                         bytes, sizeof(bytes), &frame);
    CHECK(result == GM_OK);
    CHECK(frame.abi_version == 1);
    CHECK(frame.size == 1728);
    CHECK(frame.duration_ns > 0);
  }
  CHECK(gstreamer_mojo_appsink_pull(pipeline, "sink", 0, bytes, sizeof(bytes),
                                    &frame) == GM_AGAIN);
  gm_health health;
  CHECK(gstreamer_mojo_pipeline_health(pipeline, &health) == GM_OK);
  CHECK(health.abi_version == 1);
  CHECK(health.frames_received == 3);
  CHECK(health.frames_dropped == 1);
  CHECK(health.bytes_received == 3u * 1728u);
  CHECK(health.connection_state == GM_CONNECTION_UNAVAILABLE);
  gstreamer_mojo_pipeline_note_reconnect(pipeline);
  gstreamer_mojo_pipeline_note_reconnect(pipeline);
  CHECK(gstreamer_mojo_pipeline_health(pipeline, &health) == GM_OK);
  CHECK(health.reconnect_count == 2);
  CHECK(health.frame_rate >= 0.0);
  gstreamer_mojo_pipeline_destroy(pipeline);
}

static void check_appsrc_round_trip(void) {
  gm_pipeline *pipeline = gstreamer_mojo_pipeline_create(
      "appsrc name=source is-live=false format=time ! "
      "appsink name=sink sync=false");
  CHECK(pipeline != NULL);
  CHECK(gstreamer_mojo_appsrc_set_caps(
            pipeline, "source",
            "audio/x-raw,format=S16LE,channels=1,rate=48000,layout=interleaved") ==
        GM_OK);
  CHECK(gstreamer_mojo_appsrc_set_caps(pipeline, "missing", "audio/x-raw") ==
        GM_ERROR_NOT_FOUND);
  CHECK(gstreamer_mojo_appsrc_set_caps(pipeline, "source", "not caps [") ==
        GM_ERROR_ARGUMENT);
  CHECK(gstreamer_mojo_pipeline_set_state(pipeline, GM_STATE_PLAYING,
                                           5 * NS_PER_SECOND) >= GM_OK);
  uint8_t pushed[960];
  for (size_t i = 0; i < sizeof(pushed); i++) pushed[i] = (uint8_t)(i % 251);
  CHECK(gstreamer_mojo_appsrc_push(pipeline, "source", pushed, sizeof(pushed),
                                   1234, 10000000) == GM_OK);
  uint8_t pulled[960];
  gm_frame_info frame;
  CHECK(gstreamer_mojo_appsink_pull(pipeline, "sink", 2 * NS_PER_SECOND, pulled,
                                    sizeof(pulled), &frame) == GM_OK);
  CHECK(frame.size == sizeof(pushed));
  CHECK(frame.pts_ns == 1234);
  CHECK(frame.duration_ns == 10000000);
  CHECK(memcmp(pushed, pulled, sizeof(pushed)) == 0);
  CHECK(gstreamer_mojo_appsrc_end(pipeline, "source") == GM_OK);
  int bus_result = GM_AGAIN;
  for (int i = 0; i < 10 && bus_result == GM_AGAIN; i++)
    bus_result = gstreamer_mojo_pipeline_poll_bus(pipeline, NS_PER_SECOND / 10);
  CHECK(bus_result == GM_EOS);
  gstreamer_mojo_pipeline_destroy(pipeline);
}

static void check_opus_audio_round_trip(void) {
  if (!gstreamer_mojo_has_element("opusenc") ||
      !gstreamer_mojo_has_element("opusdec"))
    return;
  gm_pipeline *pipeline = gstreamer_mojo_pipeline_create(
      "appsrc name=source is-live=false format=time "
      "caps=audio/x-raw,format=S16LE,channels=1,rate=48000,layout=interleaved ! "
      "audioconvert ! opusenc frame-size=20 ! opusparse ! opusdec ! "
      "audioconvert ! audio/x-raw,format=S16LE,channels=1,rate=48000 ! "
      "appsink name=sink sync=false");
  CHECK(pipeline != NULL);
  CHECK(gstreamer_mojo_pipeline_set_state(pipeline, GM_STATE_PLAYING,
                                           5 * NS_PER_SECOND) >= GM_OK);
  uint8_t pcm[1920] = {0};
  CHECK(gstreamer_mojo_appsrc_push(pipeline, "source", pcm, sizeof(pcm), 0,
                                   20 * 1000 * 1000) == GM_OK);
  CHECK(gstreamer_mojo_appsrc_end(pipeline, "source") == GM_OK);
  uint8_t decoded[4096];
  gm_frame_info frame;
  CHECK(gstreamer_mojo_appsink_pull(pipeline, "sink", 3 * NS_PER_SECOND,
                                    decoded, sizeof(decoded), &frame) == GM_OK);
  CHECK(frame.size > 0);
  CHECK(strcmp(frame.format, "S16LE") == 0);
  gstreamer_mojo_pipeline_destroy(pipeline);
}

static void check_h264_decode(void) {
  if (!gstreamer_mojo_has_element("x264enc") ||
      !gstreamer_mojo_has_element("avdec_h264"))
    return;
  gm_pipeline *pipeline = gstreamer_mojo_pipeline_create(
      "videotestsrc num-buffers=2 ! "
      "video/x-raw,width=64,height=36,framerate=15/1 ! "
      "x264enc tune=zerolatency speed-preset=ultrafast ! h264parse ! "
      "avdec_h264 ! videoconvert ! video/x-raw,format=RGB ! "
      "appsink name=sink sync=false");
  CHECK(pipeline != NULL);
  CHECK(gstreamer_mojo_pipeline_set_state(pipeline, GM_STATE_PLAYING,
                                           5 * NS_PER_SECOND) >= GM_OK);
  uint8_t decoded[64 * 36 * 3 + 1024];
  gm_frame_info frame;
  CHECK(gstreamer_mojo_appsink_pull(pipeline, "sink", 5 * NS_PER_SECOND,
                                    decoded, sizeof(decoded), &frame) == GM_OK);
  CHECK(frame.width == 64);
  CHECK(frame.height == 36);
  CHECK(strcmp(frame.format, "RGB") == 0);
  CHECK(frame.size == 64u * 36u * 3u);
  gstreamer_mojo_pipeline_destroy(pipeline);
}

static void check_bus_errors(void) {
  gm_pipeline *pipeline = gstreamer_mojo_pipeline_create(
      "filesrc location=/this/path/does/not/exist ! fakesink");
  CHECK(pipeline != NULL);
  int state = gstreamer_mojo_pipeline_set_state(pipeline, GM_STATE_PLAYING,
                                                 NS_PER_SECOND);
  CHECK(state == GM_ERROR_STATE || state == GM_OK || state == GM_AGAIN);
  int result = gstreamer_mojo_pipeline_poll_bus(pipeline, NS_PER_SECOND);
  CHECK(result == GM_ERROR_GSTREAMER);
  char error[512];
  CHECK(gstreamer_mojo_last_error(pipeline, error, sizeof(error)) > 0);
  CHECK(strlen(error) > 4);
  gstreamer_mojo_pipeline_destroy(pipeline);
}

static void check_webrtc(void) {
  if (!gstreamer_mojo_has_element("webrtcbin")) return;
  gm_pipeline *pipeline =
      gstreamer_mojo_pipeline_create("webrtcbin name=peer bundle-policy=max-bundle");
  CHECK(pipeline != NULL);
  CHECK(gstreamer_mojo_pipeline_set_state(pipeline, GM_STATE_READY,
                                           5 * NS_PER_SECOND) == GM_OK);
  CHECK(gstreamer_mojo_webrtc_pop_ice_candidate(pipeline, NULL, NULL, 0) ==
        GM_ERROR_ARGUMENT);
  uint32_t mline = 0;
  char candidate[4096];
  CHECK(gstreamer_mojo_webrtc_pop_ice_candidate(
            pipeline, &mline, candidate, sizeof(candidate)) == GM_AGAIN);
  int channel_result =
      gstreamer_mojo_webrtc_create_data_channel(pipeline, "peer", "commands");
  if (channel_result != GM_OK) {
    char detail[512];
    gstreamer_mojo_last_error(pipeline, detail, sizeof(detail));
    fprintf(stderr, "data channel result=%d error=%s\n", channel_result, detail);
  }
  CHECK(channel_result == GM_OK);
  CHECK(gstreamer_mojo_webrtc_create_data_channel(pipeline, "missing", "x") ==
        GM_ERROR_NOT_FOUND);
  char sdp[65536];
  int offer = gstreamer_mojo_webrtc_create_offer(pipeline, "peer", sdp,
                                                  sizeof(sdp));
  CHECK(offer > 0);
  CHECK(strstr(sdp, "v=0") != NULL);
  CHECK(strstr(sdp, "m=application") != NULL);
  CHECK(gstreamer_mojo_webrtc_create_offer(pipeline, "peer", sdp, 2) ==
        GM_ERROR_BUFFER_TOO_SMALL);
  CHECK(gstreamer_mojo_webrtc_set_remote_description(
            pipeline, "peer", "invalid", sdp) == GM_ERROR_ARGUMENT);
  CHECK(gstreamer_mojo_webrtc_set_remote_description(
            pipeline, "peer", "offer", "not valid sdp") == GM_ERROR_ARGUMENT);
  CHECK(gstreamer_mojo_webrtc_add_ice_candidate(
            pipeline, "missing", 0, "candidate:test") == GM_ERROR_NOT_FOUND);
  int send_text = gstreamer_mojo_webrtc_send_text(pipeline, "before-open");
  CHECK(send_text == GM_ERROR_STATE);
  uint8_t binary[] = {1, 2, 3};
  int send_binary =
      gstreamer_mojo_webrtc_send_binary(pipeline, binary, sizeof(binary));
  CHECK(send_binary == GM_ERROR_STATE);
  int is_text = -1;
  CHECK(gstreamer_mojo_webrtc_pop_message(pipeline, binary, sizeof(binary),
                                          &is_text) == GM_AGAIN);
  gm_health health;
  CHECK(gstreamer_mojo_pipeline_health(pipeline, &health) == GM_OK);
  CHECK(health.connection_state <= GM_CONNECTION_CLOSED);
  gstreamer_mojo_pipeline_destroy(pipeline);
}

int main(void) {
  check_runtime();
  check_arguments();
  check_lifecycle_and_properties();
  check_appsink();
  check_appsrc_round_trip();
  check_opus_audio_round_trip();
  check_h264_decode();
  check_bus_errors();
  check_webrtc();
  if (failures) {
    fprintf(stderr, "%d of %d integration assertions failed\n", failures,
            assertions);
    return 1;
  }
  printf("All %d native integration assertions passed\n", assertions);
  return 0;
}

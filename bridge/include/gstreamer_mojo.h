#ifndef GSTREAMER_MOJO_H
#define GSTREAMER_MOJO_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define GM_API __declspec(dllexport)
#else
#define GM_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define GSTREAMER_MOJO_ABI_VERSION 1u

typedef struct gm_pipeline gm_pipeline;

typedef enum gm_result {
  GM_OK = 0,
  GM_AGAIN = 1,
  GM_EOS = 2,
  GM_ERROR_ARGUMENT = -1,
  GM_ERROR_GSTREAMER = -2,
  GM_ERROR_NOT_FOUND = -3,
  GM_ERROR_BUFFER_TOO_SMALL = -4,
  GM_ERROR_STATE = -5,
  GM_ERROR_UNSUPPORTED = -6
} gm_result;

typedef enum gm_state {
  GM_STATE_VOID_PENDING = 0,
  GM_STATE_NULL = 1,
  GM_STATE_READY = 2,
  GM_STATE_PAUSED = 3,
  GM_STATE_PLAYING = 4
} gm_state;

typedef enum gm_connection_state {
  GM_CONNECTION_NEW = 0,
  GM_CONNECTION_CONNECTING = 1,
  GM_CONNECTION_CONNECTED = 2,
  GM_CONNECTION_DISCONNECTED = 3,
  GM_CONNECTION_FAILED = 4,
  GM_CONNECTION_CLOSED = 5,
  GM_CONNECTION_UNAVAILABLE = 255
} gm_connection_state;

typedef struct gm_frame_info {
  uint32_t abi_version;
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  uint32_t size;
  uint64_t pts_ns;
  uint64_t duration_ns;
  char format[16];
} gm_frame_info;

typedef struct gm_health {
  uint32_t abi_version;
  uint32_t pipeline_state;
  uint32_t connection_state;
  uint32_t reconnect_count;
  uint64_t frames_received;
  uint64_t frames_dropped;
  uint64_t bytes_received;
  uint64_t last_frame_monotonic_ns;
  double frame_rate;
} gm_health;

GM_API uint32_t gstreamer_mojo_abi_version(void);
GM_API const char *gstreamer_mojo_gstreamer_version(void);
GM_API int gstreamer_mojo_copy_gstreamer_version(char *buffer,
                                                  size_t capacity);
GM_API int gstreamer_mojo_has_element(const char *factory_name);

GM_API gm_pipeline *gstreamer_mojo_pipeline_create(const char *description);
GM_API void gstreamer_mojo_pipeline_destroy(gm_pipeline *pipeline);
GM_API int gstreamer_mojo_pipeline_set_state(gm_pipeline *pipeline,
                                               gm_state state,
                                               uint64_t timeout_ns);
GM_API int gstreamer_mojo_pipeline_get_state(gm_pipeline *pipeline);
GM_API int gstreamer_mojo_pipeline_poll_bus(gm_pipeline *pipeline,
                                             uint64_t timeout_ns);
GM_API int gstreamer_mojo_pipeline_seek(gm_pipeline *pipeline,
                                         int64_t position_ns);
GM_API int gstreamer_mojo_pipeline_set_string(gm_pipeline *pipeline,
                                               const char *element_name,
                                               const char *property_name,
                                               const char *value);
GM_API int gstreamer_mojo_pipeline_set_int(gm_pipeline *pipeline,
                                            const char *element_name,
                                            const char *property_name,
                                            int64_t value);
GM_API int gstreamer_mojo_pipeline_set_bool(gm_pipeline *pipeline,
                                             const char *element_name,
                                             const char *property_name,
                                             int value);

GM_API int gstreamer_mojo_appsink_pull(gm_pipeline *pipeline,
                                        const char *element_name,
                                        uint64_t timeout_ns, uint8_t *buffer,
                                        size_t capacity, gm_frame_info *frame);
GM_API int gstreamer_mojo_appsrc_set_caps(gm_pipeline *pipeline,
                                           const char *element_name,
                                           const char *caps);
GM_API int gstreamer_mojo_appsrc_push(gm_pipeline *pipeline,
                                      const char *element_name,
                                      const uint8_t *buffer, size_t size,
                                      uint64_t pts_ns, uint64_t duration_ns);
GM_API int gstreamer_mojo_appsrc_end(gm_pipeline *pipeline,
                                     const char *element_name);

GM_API int gstreamer_mojo_webrtc_create_offer(gm_pipeline *pipeline,
                                               const char *element_name,
                                               char *sdp, size_t capacity);
GM_API int gstreamer_mojo_webrtc_create_answer(gm_pipeline *pipeline,
                                                const char *element_name,
                                                char *sdp, size_t capacity);
GM_API int gstreamer_mojo_webrtc_set_remote_description(
    gm_pipeline *pipeline, const char *element_name, const char *type,
    const char *sdp);
GM_API int gstreamer_mojo_webrtc_add_ice_candidate(gm_pipeline *pipeline,
                                                    const char *element_name,
                                                    uint32_t mline_index,
                                                    const char *candidate);
GM_API int gstreamer_mojo_webrtc_pop_ice_candidate(
    gm_pipeline *pipeline, uint32_t *mline_index, char *candidate,
    size_t capacity);
GM_API int gstreamer_mojo_webrtc_create_data_channel(
    gm_pipeline *pipeline, const char *element_name, const char *label);
GM_API int gstreamer_mojo_webrtc_send_text(gm_pipeline *pipeline,
                                            const char *text);
GM_API int gstreamer_mojo_webrtc_send_binary(gm_pipeline *pipeline,
                                              const uint8_t *data,
                                              size_t size);
GM_API int gstreamer_mojo_webrtc_pop_message(gm_pipeline *pipeline,
                                              uint8_t *data, size_t capacity,
                                              int *is_text);

GM_API int gstreamer_mojo_pipeline_health(gm_pipeline *pipeline,
                                           gm_health *health);
GM_API void gstreamer_mojo_pipeline_note_reconnect(gm_pipeline *pipeline);
GM_API int gstreamer_mojo_last_error(gm_pipeline *pipeline, char *buffer,
                                      size_t capacity);

#ifdef __cplusplus
}
#endif

#endif

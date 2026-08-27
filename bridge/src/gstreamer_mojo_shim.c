#include "gstreamer_mojo.h"

#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static void *implementation;

static void load_implementation(void) {
  if (implementation != NULL) {
    return;
  }

  Dl_info info;
  if (dladdr((void *)&load_implementation, &info) == 0 ||
      info.dli_fname == NULL) {
    return;
  }

  char path[PATH_MAX];
  const char *slash = strrchr(info.dli_fname, '/');
  size_t directory_length = slash == NULL ? 0 : (size_t)(slash - info.dli_fname);
  int written;
  if (directory_length == 0) {
    written = snprintf(path, sizeof(path), "./libgstreamer_mojo_impl.so");
  } else {
    written = snprintf(path, sizeof(path), "%.*s/libgstreamer_mojo_impl.so",
                       (int)directory_length, info.dli_fname);
  }
  if (written < 0 || (size_t)written >= sizeof(path)) {
    return;
  }

  implementation =
      dlopen(path, RTLD_NOW | RTLD_LOCAL | RTLD_DEEPBIND);
}

static void *resolve(const char *name) {
  load_implementation();
  return implementation == NULL ? NULL : dlsym(implementation, name);
}

#define GM_RESOLVE(name, type, failure)                                         \
  type function = (type)resolve(#name);                                         \
  if (function == NULL) {                                                       \
    return failure;                                                             \
  }

uint32_t gstreamer_mojo_abi_version(void) {
  typedef uint32_t (*fn)(void);
  GM_RESOLVE(gstreamer_mojo_abi_version, fn, 0u)
  return function();
}

const char *gstreamer_mojo_gstreamer_version(void) {
  typedef const char *(*fn)(void);
  GM_RESOLVE(gstreamer_mojo_gstreamer_version, fn, NULL)
  return function();
}

int gstreamer_mojo_copy_gstreamer_version(char *buffer, size_t capacity) {
  typedef int (*fn)(char *, size_t);
  GM_RESOLVE(gstreamer_mojo_copy_gstreamer_version, fn, GM_ERROR_GSTREAMER)
  return function(buffer, capacity);
}

int gstreamer_mojo_has_element(const char *factory_name) {
  typedef int (*fn)(const char *);
  GM_RESOLVE(gstreamer_mojo_has_element, fn, 0)
  return function(factory_name);
}

gm_pipeline *gstreamer_mojo_pipeline_create(const char *description) {
  typedef gm_pipeline *(*fn)(const char *);
  GM_RESOLVE(gstreamer_mojo_pipeline_create, fn, NULL)
  return function(description);
}

void gstreamer_mojo_pipeline_destroy(gm_pipeline *pipeline) {
  typedef void (*fn)(gm_pipeline *);
  fn function = (fn)resolve("gstreamer_mojo_pipeline_destroy");
  if (function != NULL) {
    function(pipeline);
  }
}

int gstreamer_mojo_pipeline_set_state(gm_pipeline *pipeline, gm_state state,
                                      uint64_t timeout_ns) {
  typedef int (*fn)(gm_pipeline *, gm_state, uint64_t);
  GM_RESOLVE(gstreamer_mojo_pipeline_set_state, fn, GM_ERROR_GSTREAMER)
  return function(pipeline, state, timeout_ns);
}

int gstreamer_mojo_pipeline_get_state(gm_pipeline *pipeline) {
  typedef int (*fn)(gm_pipeline *);
  GM_RESOLVE(gstreamer_mojo_pipeline_get_state, fn, GM_ERROR_GSTREAMER)
  return function(pipeline);
}

int gstreamer_mojo_pipeline_poll_bus(gm_pipeline *pipeline,
                                     uint64_t timeout_ns) {
  typedef int (*fn)(gm_pipeline *, uint64_t);
  GM_RESOLVE(gstreamer_mojo_pipeline_poll_bus, fn, GM_ERROR_GSTREAMER)
  return function(pipeline, timeout_ns);
}

int gstreamer_mojo_pipeline_seek(gm_pipeline *pipeline, int64_t position_ns) {
  typedef int (*fn)(gm_pipeline *, int64_t);
  GM_RESOLVE(gstreamer_mojo_pipeline_seek, fn, GM_ERROR_GSTREAMER)
  return function(pipeline, position_ns);
}

int gstreamer_mojo_pipeline_set_string(gm_pipeline *pipeline,
                                       const char *element_name,
                                       const char *property_name,
                                       const char *value) {
  typedef int (*fn)(gm_pipeline *, const char *, const char *, const char *);
  GM_RESOLVE(gstreamer_mojo_pipeline_set_string, fn, GM_ERROR_GSTREAMER)
  return function(pipeline, element_name, property_name, value);
}

int gstreamer_mojo_pipeline_set_int(gm_pipeline *pipeline,
                                    const char *element_name,
                                    const char *property_name, int64_t value) {
  typedef int (*fn)(gm_pipeline *, const char *, const char *, int64_t);
  GM_RESOLVE(gstreamer_mojo_pipeline_set_int, fn, GM_ERROR_GSTREAMER)
  return function(pipeline, element_name, property_name, value);
}

int gstreamer_mojo_pipeline_set_bool(gm_pipeline *pipeline,
                                     const char *element_name,
                                     const char *property_name, int value) {
  typedef int (*fn)(gm_pipeline *, const char *, const char *, int);
  GM_RESOLVE(gstreamer_mojo_pipeline_set_bool, fn, GM_ERROR_GSTREAMER)
  return function(pipeline, element_name, property_name, value);
}

int gstreamer_mojo_appsink_pull(gm_pipeline *pipeline, const char *element_name,
                                uint64_t timeout_ns, uint8_t *buffer,
                                size_t capacity, gm_frame_info *frame) {
  typedef int (*fn)(gm_pipeline *, const char *, uint64_t, uint8_t *, size_t,
                    gm_frame_info *);
  GM_RESOLVE(gstreamer_mojo_appsink_pull, fn, GM_ERROR_GSTREAMER)
  return function(pipeline, element_name, timeout_ns, buffer, capacity, frame);
}

int gstreamer_mojo_appsrc_set_caps(gm_pipeline *pipeline,
                                   const char *element_name, const char *caps) {
  typedef int (*fn)(gm_pipeline *, const char *, const char *);
  GM_RESOLVE(gstreamer_mojo_appsrc_set_caps, fn, GM_ERROR_GSTREAMER)
  return function(pipeline, element_name, caps);
}

int gstreamer_mojo_appsrc_push(gm_pipeline *pipeline, const char *element_name,
                               const uint8_t *buffer, size_t size,
                               uint64_t pts_ns, uint64_t duration_ns) {
  typedef int (*fn)(gm_pipeline *, const char *, const uint8_t *, size_t,
                    uint64_t, uint64_t);
  GM_RESOLVE(gstreamer_mojo_appsrc_push, fn, GM_ERROR_GSTREAMER)
  return function(pipeline, element_name, buffer, size, pts_ns, duration_ns);
}

int gstreamer_mojo_appsrc_end(gm_pipeline *pipeline,
                              const char *element_name) {
  typedef int (*fn)(gm_pipeline *, const char *);
  GM_RESOLVE(gstreamer_mojo_appsrc_end, fn, GM_ERROR_GSTREAMER)
  return function(pipeline, element_name);
}

int gstreamer_mojo_webrtc_create_offer(gm_pipeline *pipeline,
                                       const char *element_name, char *sdp,
                                       size_t capacity) {
  typedef int (*fn)(gm_pipeline *, const char *, char *, size_t);
  GM_RESOLVE(gstreamer_mojo_webrtc_create_offer, fn, GM_ERROR_GSTREAMER)
  return function(pipeline, element_name, sdp, capacity);
}

int gstreamer_mojo_webrtc_create_answer(gm_pipeline *pipeline,
                                        const char *element_name, char *sdp,
                                        size_t capacity) {
  typedef int (*fn)(gm_pipeline *, const char *, char *, size_t);
  GM_RESOLVE(gstreamer_mojo_webrtc_create_answer, fn, GM_ERROR_GSTREAMER)
  return function(pipeline, element_name, sdp, capacity);
}

int gstreamer_mojo_webrtc_set_remote_description(
    gm_pipeline *pipeline, const char *element_name, const char *type,
    const char *sdp) {
  typedef int (*fn)(gm_pipeline *, const char *, const char *, const char *);
  GM_RESOLVE(gstreamer_mojo_webrtc_set_remote_description, fn,
             GM_ERROR_GSTREAMER)
  return function(pipeline, element_name, type, sdp);
}

int gstreamer_mojo_webrtc_add_ice_candidate(gm_pipeline *pipeline,
                                            const char *element_name,
                                            uint32_t mline_index,
                                            const char *candidate) {
  typedef int (*fn)(gm_pipeline *, const char *, uint32_t, const char *);
  GM_RESOLVE(gstreamer_mojo_webrtc_add_ice_candidate, fn, GM_ERROR_GSTREAMER)
  return function(pipeline, element_name, mline_index, candidate);
}

int gstreamer_mojo_webrtc_pop_ice_candidate(
    gm_pipeline *pipeline, uint32_t *mline_index, char *candidate,
    size_t capacity) {
  typedef int (*fn)(gm_pipeline *, uint32_t *, char *, size_t);
  GM_RESOLVE(gstreamer_mojo_webrtc_pop_ice_candidate, fn, GM_ERROR_GSTREAMER)
  return function(pipeline, mline_index, candidate, capacity);
}

int gstreamer_mojo_webrtc_create_data_channel(gm_pipeline *pipeline,
                                              const char *element_name,
                                              const char *label) {
  typedef int (*fn)(gm_pipeline *, const char *, const char *);
  GM_RESOLVE(gstreamer_mojo_webrtc_create_data_channel, fn,
             GM_ERROR_GSTREAMER)
  return function(pipeline, element_name, label);
}

int gstreamer_mojo_webrtc_send_text(gm_pipeline *pipeline, const char *text) {
  typedef int (*fn)(gm_pipeline *, const char *);
  GM_RESOLVE(gstreamer_mojo_webrtc_send_text, fn, GM_ERROR_GSTREAMER)
  return function(pipeline, text);
}

int gstreamer_mojo_webrtc_send_binary(gm_pipeline *pipeline,
                                      const uint8_t *data, size_t size) {
  typedef int (*fn)(gm_pipeline *, const uint8_t *, size_t);
  GM_RESOLVE(gstreamer_mojo_webrtc_send_binary, fn, GM_ERROR_GSTREAMER)
  return function(pipeline, data, size);
}

int gstreamer_mojo_webrtc_pop_message(gm_pipeline *pipeline, uint8_t *data,
                                      size_t capacity, int *is_text) {
  typedef int (*fn)(gm_pipeline *, uint8_t *, size_t, int *);
  GM_RESOLVE(gstreamer_mojo_webrtc_pop_message, fn, GM_ERROR_GSTREAMER)
  return function(pipeline, data, capacity, is_text);
}

int gstreamer_mojo_pipeline_health(gm_pipeline *pipeline, gm_health *health) {
  typedef int (*fn)(gm_pipeline *, gm_health *);
  GM_RESOLVE(gstreamer_mojo_pipeline_health, fn, GM_ERROR_GSTREAMER)
  return function(pipeline, health);
}

void gstreamer_mojo_pipeline_note_reconnect(gm_pipeline *pipeline) {
  typedef void (*fn)(gm_pipeline *);
  fn function = (fn)resolve("gstreamer_mojo_pipeline_note_reconnect");
  if (function != NULL) {
    function(pipeline);
  }
}

int gstreamer_mojo_last_error(gm_pipeline *pipeline, char *buffer,
                              size_t capacity) {
  typedef int (*fn)(gm_pipeline *, char *, size_t);
  GM_RESOLVE(gstreamer_mojo_last_error, fn, GM_ERROR_GSTREAMER)
  return function(pipeline, buffer, capacity);
}

#include "gstreamer_mojo.h"

#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gst/sdp/gstsdpmessage.h>
#include <gst/webrtc/webrtc.h>
#include <string.h>

typedef struct gm_ice {
  guint mline;
  char *candidate;
} gm_ice;

typedef struct gm_message {
  gboolean is_text;
  GBytes *bytes;
} gm_message;

struct gm_pipeline {
  GstElement *pipeline;
  GstBus *bus;
  GMutex lock;
  char *last_error;
  GAsyncQueue *ice;
  GAsyncQueue *messages;
  GstWebRTCDataChannel *channel;
  guint64 frames_received;
  guint64 frames_dropped;
  guint64 bytes_received;
  guint64 first_frame_ns;
  guint64 last_frame_ns;
  guint reconnect_count;
};

static gsize initialized = 0;

static void gm_initialize(void) {
  if (g_once_init_enter(&initialized)) {
    gst_init(NULL, NULL);
    g_once_init_leave(&initialized, 1);
  }
}

static void gm_set_error(gm_pipeline *self, const char *message) {
  if (!self) return;
  g_mutex_lock(&self->lock);
  g_free(self->last_error);
  self->last_error = g_strdup(message ? message : "unknown GStreamer error");
  g_mutex_unlock(&self->lock);
}

static GstElement *gm_element(gm_pipeline *self, const char *name,
                              GType expected) {
  if (!self || !name || !*name) return NULL;
  GstElement *element = NULL;
  if (g_str_equal(GST_OBJECT_NAME(self->pipeline), name))
    element = gst_object_ref(self->pipeline);
  else if (GST_IS_BIN(self->pipeline))
    element = gst_bin_get_by_name(GST_BIN(self->pipeline), name);
  if (!element) {
    gm_set_error(self, "named pipeline element was not found");
    return NULL;
  }
  if (expected != G_TYPE_INVALID && !g_type_is_a(G_OBJECT_TYPE(element), expected)) {
    gm_set_error(self, "named pipeline element has the wrong type");
    gst_object_unref(element);
    return NULL;
  }
  return element;
}

static gboolean gm_is_factory(GstElement *element, const char *name) {
  GstElementFactory *factory = gst_element_get_factory(element);
  return factory &&
         g_str_equal(gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)),
                     name);
}

static GstElement *gm_webrtc(gm_pipeline *self, const char *name) {
  GstElement *element = gm_element(self, name, G_TYPE_INVALID);
  if (element && !gm_is_factory(element, "webrtcbin")) {
    gm_set_error(self, "named pipeline element is not a webrtcbin");
    gst_object_unref(element);
    return NULL;
  }
  return element;
}

static int gm_copy_string(const char *value, char *buffer, size_t capacity) {
  size_t required = strlen(value) + 1;
  if (!buffer || capacity < required) return GM_ERROR_BUFFER_TOO_SMALL;
  memcpy(buffer, value, required);
  return (int)(required - 1);
}

static void gm_ice_free(gpointer data) {
  gm_ice *ice = data;
  if (!ice) return;
  g_free(ice->candidate);
  g_free(ice);
}

static void gm_message_free(gpointer data) {
  gm_message *message = data;
  if (!message) return;
  g_bytes_unref(message->bytes);
  g_free(message);
}

static void gm_on_ice(GstElement *webrtc, guint mline, gchar *candidate,
                      gpointer user_data) {
  (void)webrtc;
  gm_pipeline *self = user_data;
  gm_ice *ice = g_new0(gm_ice, 1);
  ice->mline = mline;
  ice->candidate = g_strdup(candidate ? candidate : "");
  g_async_queue_push(self->ice, ice);
}

static void gm_on_message_string(GstWebRTCDataChannel *channel, gchar *text,
                                 gpointer user_data) {
  (void)channel;
  gm_pipeline *self = user_data;
  gm_message *message = g_new0(gm_message, 1);
  message->is_text = TRUE;
  message->bytes = g_bytes_new(text ? text : "", text ? strlen(text) : 0);
  g_async_queue_push(self->messages, message);
}

static void gm_on_message_data(GstWebRTCDataChannel *channel, GBytes *bytes,
                               gpointer user_data) {
  (void)channel;
  gm_pipeline *self = user_data;
  gm_message *message = g_new0(gm_message, 1);
  message->is_text = FALSE;
  message->bytes = g_bytes_ref(bytes);
  g_async_queue_push(self->messages, message);
}

static void gm_attach_channel(gm_pipeline *self,
                              GstWebRTCDataChannel *channel) {
  if (!channel) return;
  g_mutex_lock(&self->lock);
  if (self->channel) g_object_unref(self->channel);
  self->channel = g_object_ref(channel);
  g_mutex_unlock(&self->lock);
  g_signal_connect(channel, "on-message-string",
                   G_CALLBACK(gm_on_message_string), self);
  g_signal_connect(channel, "on-message-data", G_CALLBACK(gm_on_message_data),
                   self);
}

static void gm_on_data_channel(GstElement *webrtc,
                               GstWebRTCDataChannel *channel,
                               gpointer user_data) {
  (void)webrtc;
  gm_attach_channel(user_data, channel);
}

uint32_t gstreamer_mojo_abi_version(void) {
  return GSTREAMER_MOJO_ABI_VERSION;
}

const char *gstreamer_mojo_gstreamer_version(void) {
  gm_initialize();
  return gst_version_string();
}

int gstreamer_mojo_copy_gstreamer_version(char *buffer, size_t capacity) {
  return gm_copy_string(gstreamer_mojo_gstreamer_version(), buffer, capacity);
}

int gstreamer_mojo_has_element(const char *factory_name) {
  gm_initialize();
  if (!factory_name) return 0;
  GstElementFactory *factory = gst_element_factory_find(factory_name);
  if (!factory) return 0;
  gst_object_unref(factory);
  return 1;
}

gm_pipeline *gstreamer_mojo_pipeline_create(const char *description) {
  gm_initialize();
  if (!description || !*description) return NULL;
  gm_pipeline *self = g_new0(gm_pipeline, 1);
  g_mutex_init(&self->lock);
  self->ice = g_async_queue_new_full(gm_ice_free);
  self->messages = g_async_queue_new_full(gm_message_free);
  GError *error = NULL;
  self->pipeline = gst_parse_launch(description, &error);
  if (!self->pipeline) {
    gm_set_error(self, error ? error->message : "could not parse pipeline");
    if (error) g_error_free(error);
    gstreamer_mojo_pipeline_destroy(self);
    return NULL;
  }
  if (error) {
    gm_set_error(self, error->message);
    g_error_free(error);
  }
  self->bus = gst_element_get_bus(self->pipeline);
  if (gm_is_factory(self->pipeline, "webrtcbin")) {
    g_signal_connect(self->pipeline, "on-ice-candidate", G_CALLBACK(gm_on_ice),
                     self);
    g_signal_connect(self->pipeline, "on-data-channel",
                     G_CALLBACK(gm_on_data_channel), self);
  }
  GstIterator *iterator = gst_bin_iterate_elements(GST_BIN(self->pipeline));
  GValue item = G_VALUE_INIT;
  while (gst_iterator_next(iterator, &item) == GST_ITERATOR_OK) {
    GstElement *element = g_value_get_object(&item);
    if (gm_is_factory(element, "webrtcbin")) {
      g_signal_connect(element, "on-ice-candidate", G_CALLBACK(gm_on_ice), self);
      g_signal_connect(element, "on-data-channel", G_CALLBACK(gm_on_data_channel),
                       self);
    }
    g_value_reset(&item);
  }
  g_value_unset(&item);
  gst_iterator_free(iterator);
  return self;
}

void gstreamer_mojo_pipeline_destroy(gm_pipeline *self) {
  if (!self) return;
  if (self->pipeline) gst_element_set_state(self->pipeline, GST_STATE_NULL);
  if (self->channel) g_object_unref(self->channel);
  if (self->bus) gst_object_unref(self->bus);
  if (self->pipeline) gst_object_unref(self->pipeline);
  if (self->ice) g_async_queue_unref(self->ice);
  if (self->messages) g_async_queue_unref(self->messages);
  g_free(self->last_error);
  g_mutex_clear(&self->lock);
  g_free(self);
}

int gstreamer_mojo_pipeline_set_state(gm_pipeline *self, gm_state state,
                                      uint64_t timeout_ns) {
  if (!self || state < GM_STATE_NULL || state > GM_STATE_PLAYING)
    return GM_ERROR_ARGUMENT;
  GstState target = (GstState)state;
  GstStateChangeReturn result = gst_element_set_state(self->pipeline, target);
  if (result == GST_STATE_CHANGE_FAILURE) {
    gm_set_error(self, "pipeline state change failed");
    return GM_ERROR_STATE;
  }
  GstState current = GST_STATE_VOID_PENDING;
  result = gst_element_get_state(self->pipeline, &current, NULL,
                                 (GstClockTime)timeout_ns);
  if (result == GST_STATE_CHANGE_FAILURE) {
    gm_set_error(self, "pipeline did not reach the requested state");
    return GM_ERROR_STATE;
  }
  return (result == GST_STATE_CHANGE_ASYNC) ? GM_AGAIN : GM_OK;
}

int gstreamer_mojo_pipeline_get_state(gm_pipeline *self) {
  if (!self) return GM_ERROR_ARGUMENT;
  GstState current = GST_STATE_VOID_PENDING;
  gst_element_get_state(self->pipeline, &current, NULL, 0);
  return (int)current;
}

int gstreamer_mojo_pipeline_poll_bus(gm_pipeline *self, uint64_t timeout_ns) {
  if (!self) return GM_ERROR_ARGUMENT;
  GstMessage *message = gst_bus_timed_pop_filtered(
      self->bus, (GstClockTime)timeout_ns,
      GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_WARNING);
  if (!message) return GM_AGAIN;
  int result = GM_OK;
  if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
    result = GM_EOS;
  } else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
    GError *error = NULL;
    gchar *debug = NULL;
    gst_message_parse_error(message, &error, &debug);
    gm_set_error(self, error ? error->message : "GStreamer bus error");
    if (error) g_error_free(error);
    g_free(debug);
    result = GM_ERROR_GSTREAMER;
  } else {
    GError *warning = NULL;
    gchar *debug = NULL;
    gst_message_parse_warning(message, &warning, &debug);
    gm_set_error(self, warning ? warning->message : "GStreamer warning");
    if (warning) g_error_free(warning);
    g_free(debug);
  }
  gst_message_unref(message);
  return result;
}

int gstreamer_mojo_pipeline_seek(gm_pipeline *self, int64_t position_ns) {
  if (!self || position_ns < 0) return GM_ERROR_ARGUMENT;
  if (!gst_element_seek_simple(self->pipeline, GST_FORMAT_TIME,
                               GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
                               position_ns)) {
    gm_set_error(self, "pipeline seek failed");
    return GM_ERROR_STATE;
  }
  return GM_OK;
}

static int gm_property(gm_pipeline *self, const char *element_name,
                       const char *property_name, const GValue *value) {
  if (!self || !element_name || !property_name || !value)
    return GM_ERROR_ARGUMENT;
  GstElement *element = gm_element(self, element_name, G_TYPE_INVALID);
  if (!element) return GM_ERROR_NOT_FOUND;
  GParamSpec *spec = g_object_class_find_property(G_OBJECT_GET_CLASS(element),
                                                   property_name);
  if (!spec) {
    gm_set_error(self, "element property was not found");
    gst_object_unref(element);
    return GM_ERROR_NOT_FOUND;
  }
  GValue converted = G_VALUE_INIT;
  g_value_init(&converted, spec->value_type);
  if (!g_value_transform(value, &converted)) {
    gm_set_error(self, "property value has an incompatible type");
    g_value_unset(&converted);
    gst_object_unref(element);
    return GM_ERROR_ARGUMENT;
  }
  g_object_set_property(G_OBJECT(element), property_name, &converted);
  g_value_unset(&converted);
  gst_object_unref(element);
  return GM_OK;
}

int gstreamer_mojo_pipeline_set_string(gm_pipeline *self,
                                       const char *element_name,
                                       const char *property_name,
                                       const char *value) {
  if (!value) return GM_ERROR_ARGUMENT;
  GValue input = G_VALUE_INIT;
  g_value_init(&input, G_TYPE_STRING);
  g_value_set_string(&input, value);
  int result = gm_property(self, element_name, property_name, &input);
  g_value_unset(&input);
  return result;
}

int gstreamer_mojo_pipeline_set_int(gm_pipeline *self,
                                    const char *element_name,
                                    const char *property_name, int64_t value) {
  GValue input = G_VALUE_INIT;
  g_value_init(&input, G_TYPE_INT64);
  g_value_set_int64(&input, value);
  int result = gm_property(self, element_name, property_name, &input);
  g_value_unset(&input);
  return result;
}

int gstreamer_mojo_pipeline_set_bool(gm_pipeline *self,
                                     const char *element_name,
                                     const char *property_name, int value) {
  GValue input = G_VALUE_INIT;
  g_value_init(&input, G_TYPE_BOOLEAN);
  g_value_set_boolean(&input, value != 0);
  int result = gm_property(self, element_name, property_name, &input);
  g_value_unset(&input);
  return result;
}

int gstreamer_mojo_appsink_pull(gm_pipeline *self, const char *element_name,
                                uint64_t timeout_ns, uint8_t *buffer,
                                size_t capacity, gm_frame_info *frame) {
  if (!self || !buffer || !frame) return GM_ERROR_ARGUMENT;
  GstElement *element = gm_element(self, element_name, GST_TYPE_APP_SINK);
  if (!element) return GM_ERROR_NOT_FOUND;
  GstSample *sample = gst_app_sink_try_pull_sample(GST_APP_SINK(element),
                                                   timeout_ns);
  gst_object_unref(element);
  if (!sample) return GM_AGAIN;
  GstBuffer *gst_buffer = gst_sample_get_buffer(sample);
  GstMapInfo map;
  if (!gst_buffer_map(gst_buffer, &map, GST_MAP_READ)) {
    gst_sample_unref(sample);
    gm_set_error(self, "could not map appsink sample");
    return GM_ERROR_GSTREAMER;
  }
  memset(frame, 0, sizeof(*frame));
  frame->abi_version = GSTREAMER_MOJO_ABI_VERSION;
  frame->size = (uint32_t)map.size;
  frame->pts_ns = GST_BUFFER_PTS_IS_VALID(gst_buffer) ? GST_BUFFER_PTS(gst_buffer) : UINT64_MAX;
  frame->duration_ns = GST_BUFFER_DURATION_IS_VALID(gst_buffer) ? GST_BUFFER_DURATION(gst_buffer) : UINT64_MAX;
  GstCaps *caps = gst_sample_get_caps(sample);
  if (caps && gst_caps_get_size(caps) > 0) {
    const GstStructure *structure = gst_caps_get_structure(caps, 0);
    gint width = 0;
    gint height = 0;
    gst_structure_get_int(structure, "width", &width);
    gst_structure_get_int(structure, "height", &height);
    frame->width = width > 0 ? (uint32_t)width : 0;
    frame->height = height > 0 ? (uint32_t)height : 0;
    const char *format = gst_structure_get_string(structure, "format");
    if (format) g_strlcpy(frame->format, format, sizeof(frame->format));
    if (frame->height && map.size % frame->height == 0)
      frame->stride = (uint32_t)(map.size / frame->height);
  }
  int result = GM_OK;
  if (map.size > capacity) {
    result = GM_ERROR_BUFFER_TOO_SMALL;
    g_mutex_lock(&self->lock);
    self->frames_dropped++;
    g_mutex_unlock(&self->lock);
  } else {
    memcpy(buffer, map.data, map.size);
    guint64 now = gst_util_get_timestamp();
    g_mutex_lock(&self->lock);
    self->frames_received++;
    self->bytes_received += map.size;
    if (!self->first_frame_ns) self->first_frame_ns = now;
    self->last_frame_ns = now;
    g_mutex_unlock(&self->lock);
  }
  gst_buffer_unmap(gst_buffer, &map);
  gst_sample_unref(sample);
  return result;
}

int gstreamer_mojo_appsrc_set_caps(gm_pipeline *self, const char *element_name,
                                   const char *caps_text) {
  if (!self || !caps_text) return GM_ERROR_ARGUMENT;
  GstElement *element = gm_element(self, element_name, GST_TYPE_APP_SRC);
  if (!element) return GM_ERROR_NOT_FOUND;
  GstCaps *caps = gst_caps_from_string(caps_text);
  if (!caps) {
    gst_object_unref(element);
    gm_set_error(self, "invalid appsrc caps");
    return GM_ERROR_ARGUMENT;
  }
  gst_app_src_set_caps(GST_APP_SRC(element), caps);
  gst_caps_unref(caps);
  gst_object_unref(element);
  return GM_OK;
}

int gstreamer_mojo_appsrc_push(gm_pipeline *self, const char *element_name,
                               const uint8_t *buffer, size_t size,
                               uint64_t pts_ns, uint64_t duration_ns) {
  if (!self || (!buffer && size)) return GM_ERROR_ARGUMENT;
  GstElement *element = gm_element(self, element_name, GST_TYPE_APP_SRC);
  if (!element) return GM_ERROR_NOT_FOUND;
  GstBuffer *gst_buffer = gst_buffer_new_allocate(NULL, size, NULL);
  if (!gst_buffer) {
    gst_object_unref(element);
    return GM_ERROR_GSTREAMER;
  }
  gst_buffer_fill(gst_buffer, 0, buffer, size);
  GST_BUFFER_PTS(gst_buffer) = pts_ns == UINT64_MAX ? GST_CLOCK_TIME_NONE : pts_ns;
  GST_BUFFER_DURATION(gst_buffer) = duration_ns == UINT64_MAX ? GST_CLOCK_TIME_NONE : duration_ns;
  GstFlowReturn flow = gst_app_src_push_buffer(GST_APP_SRC(element), gst_buffer);
  gst_object_unref(element);
  if (flow != GST_FLOW_OK) {
    gm_set_error(self, gst_flow_get_name(flow));
    return GM_ERROR_GSTREAMER;
  }
  return GM_OK;
}

int gstreamer_mojo_appsrc_end(gm_pipeline *self, const char *element_name) {
  if (!self) return GM_ERROR_ARGUMENT;
  GstElement *element = gm_element(self, element_name, GST_TYPE_APP_SRC);
  if (!element) return GM_ERROR_NOT_FOUND;
  GstFlowReturn flow = gst_app_src_end_of_stream(GST_APP_SRC(element));
  gst_object_unref(element);
  return flow == GST_FLOW_OK ? GM_OK : GM_ERROR_GSTREAMER;
}

static int gm_create_description(gm_pipeline *self, const char *element_name,
                                 const char *action, const char *field,
                                 char *sdp_text, size_t capacity) {
  if (!self || !sdp_text) return GM_ERROR_ARGUMENT;
  GstElement *webrtc = gm_webrtc(self, element_name);
  if (!webrtc) return GM_ERROR_NOT_FOUND;
  GstPromise *promise = gst_promise_new();
  g_signal_emit_by_name(webrtc, action, NULL, promise);
  GstPromiseResult waited = gst_promise_wait(promise);
  if (waited != GST_PROMISE_RESULT_REPLIED) {
    gst_promise_unref(promise);
    gst_object_unref(webrtc);
    gm_set_error(self, "WebRTC SDP promise did not produce a reply");
    return GM_ERROR_GSTREAMER;
  }
  const GstStructure *reply = gst_promise_get_reply(promise);
  GstWebRTCSessionDescription *description = NULL;
  gst_structure_get(reply, field, GST_TYPE_WEBRTC_SESSION_DESCRIPTION,
                    &description, NULL);
  if (!description) {
    gst_promise_unref(promise);
    gst_object_unref(webrtc);
    gm_set_error(self, "WebRTC SDP reply was empty");
    return GM_ERROR_GSTREAMER;
  }
  GstPromise *local = gst_promise_new();
  g_signal_emit_by_name(webrtc, "set-local-description", description, local);
  gst_promise_wait(local);
  gst_promise_unref(local);
  gchar *text = gst_sdp_message_as_text(description->sdp);
  int result = gm_copy_string(text, sdp_text, capacity);
  g_free(text);
  gst_webrtc_session_description_free(description);
  gst_promise_unref(promise);
  gst_object_unref(webrtc);
  return result;
}

int gstreamer_mojo_webrtc_create_offer(gm_pipeline *self,
                                       const char *element_name, char *sdp,
                                       size_t capacity) {
  return gm_create_description(self, element_name, "create-offer", "offer",
                               sdp, capacity);
}

int gstreamer_mojo_webrtc_create_answer(gm_pipeline *self,
                                        const char *element_name, char *sdp,
                                        size_t capacity) {
  return gm_create_description(self, element_name, "create-answer", "answer",
                               sdp, capacity);
}

int gstreamer_mojo_webrtc_set_remote_description(
    gm_pipeline *self, const char *element_name, const char *type,
    const char *sdp_text) {
  if (!self || !type || !sdp_text) return GM_ERROR_ARGUMENT;
  if (!g_str_has_prefix(sdp_text, "v=")) {
    gm_set_error(self, "invalid remote SDP");
    return GM_ERROR_ARGUMENT;
  }
  GstWebRTCSDPType sdp_type;
  if (g_str_equal(type, "offer")) sdp_type = GST_WEBRTC_SDP_TYPE_OFFER;
  else if (g_str_equal(type, "answer")) sdp_type = GST_WEBRTC_SDP_TYPE_ANSWER;
  else return GM_ERROR_ARGUMENT;
  GstSDPMessage *sdp = NULL;
  if (gst_sdp_message_new(&sdp) != GST_SDP_OK ||
      gst_sdp_message_parse_buffer((const guint8 *)sdp_text, strlen(sdp_text),
                                   sdp) != GST_SDP_OK) {
    if (sdp) gst_sdp_message_free(sdp);
    gm_set_error(self, "invalid remote SDP");
    return GM_ERROR_ARGUMENT;
  }
  GstElement *webrtc = gm_webrtc(self, element_name);
  if (!webrtc) {
    gst_sdp_message_free(sdp);
    return GM_ERROR_NOT_FOUND;
  }
  GstWebRTCSessionDescription *description =
      gst_webrtc_session_description_new(sdp_type, sdp);
  GstPromise *promise = gst_promise_new();
  g_signal_emit_by_name(webrtc, "set-remote-description", description, promise);
  GstPromiseResult result = gst_promise_wait(promise);
  gst_promise_unref(promise);
  gst_webrtc_session_description_free(description);
  gst_object_unref(webrtc);
  return result == GST_PROMISE_RESULT_REPLIED ? GM_OK : GM_ERROR_GSTREAMER;
}

int gstreamer_mojo_webrtc_add_ice_candidate(gm_pipeline *self,
                                            const char *element_name,
                                            uint32_t mline_index,
                                            const char *candidate) {
  if (!self || !candidate) return GM_ERROR_ARGUMENT;
  GstElement *webrtc = gm_webrtc(self, element_name);
  if (!webrtc) return GM_ERROR_NOT_FOUND;
  g_signal_emit_by_name(webrtc, "add-ice-candidate", mline_index, candidate);
  gst_object_unref(webrtc);
  return GM_OK;
}

int gstreamer_mojo_webrtc_pop_ice_candidate(gm_pipeline *self,
                                            uint32_t *mline_index,
                                            char *candidate, size_t capacity) {
  if (!self || !mline_index || !candidate) return GM_ERROR_ARGUMENT;
  gm_ice *ice = g_async_queue_try_pop(self->ice);
  if (!ice) return GM_AGAIN;
  *mline_index = ice->mline;
  int result = gm_copy_string(ice->candidate, candidate, capacity);
  gm_ice_free(ice);
  return result;
}

int gstreamer_mojo_webrtc_create_data_channel(gm_pipeline *self,
                                              const char *element_name,
                                              const char *label) {
  if (!self || !label) return GM_ERROR_ARGUMENT;
  GstElement *webrtc = gm_webrtc(self, element_name);
  if (!webrtc) return GM_ERROR_NOT_FOUND;
  GstWebRTCDataChannel *channel = NULL;
  g_signal_emit_by_name(webrtc, "create-data-channel", label, NULL, &channel);
  gst_object_unref(webrtc);
  if (!channel) {
    gm_set_error(self, "could not create WebRTC data channel");
    return GM_ERROR_GSTREAMER;
  }
  gm_attach_channel(self, channel);
  g_object_unref(channel);
  return GM_OK;
}

int gstreamer_mojo_webrtc_send_text(gm_pipeline *self, const char *text) {
  if (!self || !text) return GM_ERROR_ARGUMENT;
  g_mutex_lock(&self->lock);
  GstWebRTCDataChannel *channel = self->channel ? g_object_ref(self->channel) : NULL;
  g_mutex_unlock(&self->lock);
  if (!channel) return GM_ERROR_STATE;
  GstWebRTCDataChannelState ready = GST_WEBRTC_DATA_CHANNEL_STATE_CONNECTING;
  g_object_get(channel, "ready-state", &ready, NULL);
  if (ready != GST_WEBRTC_DATA_CHANNEL_STATE_OPEN) {
    g_object_unref(channel);
    gm_set_error(self, "WebRTC data channel is not open");
    return GM_ERROR_STATE;
  }
  gst_webrtc_data_channel_send_string(channel, text);
  g_object_unref(channel);
  return GM_OK;
}

int gstreamer_mojo_webrtc_send_binary(gm_pipeline *self, const uint8_t *data,
                                      size_t size) {
  if (!self || (!data && size)) return GM_ERROR_ARGUMENT;
  g_mutex_lock(&self->lock);
  GstWebRTCDataChannel *channel = self->channel ? g_object_ref(self->channel) : NULL;
  g_mutex_unlock(&self->lock);
  if (!channel) return GM_ERROR_STATE;
  GstWebRTCDataChannelState ready = GST_WEBRTC_DATA_CHANNEL_STATE_CONNECTING;
  g_object_get(channel, "ready-state", &ready, NULL);
  if (ready != GST_WEBRTC_DATA_CHANNEL_STATE_OPEN) {
    g_object_unref(channel);
    gm_set_error(self, "WebRTC data channel is not open");
    return GM_ERROR_STATE;
  }
  GBytes *bytes = g_bytes_new(data, size);
  gst_webrtc_data_channel_send_data(channel, bytes);
  g_bytes_unref(bytes);
  g_object_unref(channel);
  return GM_OK;
}

int gstreamer_mojo_webrtc_pop_message(gm_pipeline *self, uint8_t *data,
                                      size_t capacity, int *is_text) {
  if (!self || !data || !is_text) return GM_ERROR_ARGUMENT;
  gm_message *message = g_async_queue_try_pop(self->messages);
  if (!message) return GM_AGAIN;
  gsize size = 0;
  const guint8 *bytes = g_bytes_get_data(message->bytes, &size);
  if (size > capacity) {
    gm_message_free(message);
    return GM_ERROR_BUFFER_TOO_SMALL;
  }
  memcpy(data, bytes, size);
  *is_text = message->is_text ? 1 : 0;
  gm_message_free(message);
  return (int)size;
}

int gstreamer_mojo_pipeline_health(gm_pipeline *self, gm_health *health) {
  if (!self || !health) return GM_ERROR_ARGUMENT;
  memset(health, 0, sizeof(*health));
  health->abi_version = GSTREAMER_MOJO_ABI_VERSION;
  health->pipeline_state = (uint32_t)gstreamer_mojo_pipeline_get_state(self);
  GstElement *webrtc = NULL;
  if (gm_is_factory(self->pipeline, "webrtcbin"))
    webrtc = gst_object_ref(self->pipeline);
  GstIterator *iterator = gst_bin_iterate_elements(GST_BIN(self->pipeline));
  GValue item = G_VALUE_INIT;
  while (gst_iterator_next(iterator, &item) == GST_ITERATOR_OK) {
    GstElement *candidate = g_value_get_object(&item);
    if (!webrtc && gm_is_factory(candidate, "webrtcbin")) {
      webrtc = gst_object_ref(candidate);
      g_value_reset(&item);
      break;
    }
    g_value_reset(&item);
  }
  g_value_unset(&item);
  gst_iterator_free(iterator);
  if (webrtc) {
    GstWebRTCPeerConnectionState state = GST_WEBRTC_PEER_CONNECTION_STATE_NEW;
    g_object_get(webrtc, "connection-state", &state, NULL);
    health->connection_state = (uint32_t)state;
    gst_object_unref(webrtc);
  } else {
    health->connection_state = GM_CONNECTION_UNAVAILABLE;
  }
  g_mutex_lock(&self->lock);
  health->reconnect_count = self->reconnect_count;
  health->frames_received = self->frames_received;
  health->frames_dropped = self->frames_dropped;
  health->bytes_received = self->bytes_received;
  health->last_frame_monotonic_ns = self->last_frame_ns;
  if (self->frames_received > 1 && self->last_frame_ns > self->first_frame_ns) {
    health->frame_rate = (double)(self->frames_received - 1) * GST_SECOND /
                         (double)(self->last_frame_ns - self->first_frame_ns);
  }
  g_mutex_unlock(&self->lock);
  return GM_OK;
}

void gstreamer_mojo_pipeline_note_reconnect(gm_pipeline *self) {
  if (!self) return;
  g_mutex_lock(&self->lock);
  self->reconnect_count++;
  g_mutex_unlock(&self->lock);
}

int gstreamer_mojo_last_error(gm_pipeline *self, char *buffer,
                              size_t capacity) {
  if (!self || !buffer) return GM_ERROR_ARGUMENT;
  g_mutex_lock(&self->lock);
  int result = gm_copy_string(self->last_error ? self->last_error : "", buffer,
                              capacity);
  g_mutex_unlock(&self->lock);
  return result;
}

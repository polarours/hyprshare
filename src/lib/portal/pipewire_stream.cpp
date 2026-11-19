#include "hyprshare/portal/pipewire_stream.hpp"

#include <pipewire/pipewire.h>
#include <spa/param/format-utils.h>
#include <spa/param/video/format-utils.h>
#include <spa/utils/hook.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>

namespace hyprshare::portal {

struct PipeWireStream::Impl {
  pw_thread_loop* thread_loop = nullptr;
  pw_context* context = nullptr;
  pw_core* core = nullptr;
  pw_stream* stream = nullptr;
  pw_stream_state state = PW_STREAM_STATE_UNCONNECTED;
  uint32_t node_id = 0;
  bool connected = false;
  bool started = false;
  bool loop_running = false;
  FrameCallback callback;
  VideoFormat video_format;
  bool format_negotiated = false;
  uint64_t frame_count = 0;

  struct spa_hook listener{};
  pw_stream_events events{};

  Impl() {
    events.version = PW_VERSION_STREAM_EVENTS;
    events.process = &Impl::on_process;
    events.state_changed = &Impl::on_state_changed;
    events.param_changed = &Impl::on_param_changed;
  }

  // The capture source announces the format it can deliver. Accepting it is
  // what makes the stream produce frames at all.
  static void on_param_changed(void* userdata, const uint32_t id, const struct spa_pod* param) {
    auto* impl = static_cast<Impl*>(userdata);
    if (id != SPA_PARAM_EnumFormat || param == nullptr) {
      return;
    }

    struct spa_video_info_raw info = {};
    if (spa_format_video_raw_parse(param, &info) < 0) {
      return;
    }

    impl->video_format.width = info.size.width;
    impl->video_format.height = info.size.height;
    impl->video_format.format = static_cast<uint32_t>(info.format);
    impl->video_format.stride = 0;
    impl->video_format.framerate_num = info.framerate.num;
    impl->video_format.framerate_denom = info.framerate.denom == 0 ? 1 : info.framerate.denom;
    impl->video_format.frame_size = static_cast<uint64_t>(info.size.width) *
      static_cast<uint64_t>(info.size.height) * 4u;
    impl->format_negotiated = true;

    const struct spa_pod* params[1] = { param };
    pw_stream_update_params(impl->stream, params, 1);
  }

  static void on_process(void* userdata) {
    auto* impl = static_cast<Impl*>(userdata);
    if (!impl->stream || !impl->callback) {
      return;
    }

    pw_buffer* buf = pw_stream_dequeue_buffer(impl->stream);
    if (!buf || buf->buffer == nullptr) {
      if (buf) {
        pw_stream_queue_buffer(impl->stream, buf);
      }
      return;
    }

    auto* spa_buf = buf->buffer;
    auto* data = spa_buf->datas[0].data;
    const auto size = spa_buf->datas[0].chunk ? spa_buf->datas[0].chunk->size : 0;

    if (size > 0 && data) {
      FrameInfo info;
      info.width = impl->video_format.width;
      info.height = impl->video_format.height;
      info.format = impl->video_format.format;
      // The stride of the chunk tells how the rows are padded.
      if (spa_buf->datas[0].chunk != nullptr && spa_buf->datas[0].chunk->stride > 0) {
        impl->video_format.stride = spa_buf->datas[0].chunk->stride;
      }
      info.stride = impl->video_format.stride;
      // The presentation timestamp is counted from the first frame, which is
      // enough for a consumer that generates its own timing.
      info.pts = ++impl->frame_count;
      impl->callback(data, static_cast<uint32_t>(size), info);
    }

    pw_stream_queue_buffer(impl->stream, buf);
  }

  static void on_state_changed(void* userdata, pw_stream_state, pw_stream_state new_state, const char* error) {
    auto* impl = static_cast<Impl*>(userdata);
    impl->state = new_state;

    if (new_state == PW_STREAM_STATE_STREAMING) {
      impl->connected = true;
    } else if (new_state == PW_STREAM_STATE_ERROR || new_state == PW_STREAM_STATE_UNCONNECTED) {
      impl->connected = false;
      if (new_state == PW_STREAM_STATE_ERROR && error) {
        std::cerr << "pipewire stream error: " << error << "\n";
      }
    }
  }
};

PipeWireStream::PipeWireStream() : impl_(std::make_unique<Impl>()) {}

PipeWireStream::~PipeWireStream() {
  disconnect();
}

std::expected<void, std::string> PipeWireStream::connect(const uint32_t node_id) {
  impl_->node_id = node_id;

  pw_init(nullptr, nullptr);

  impl_->thread_loop = pw_thread_loop_new("hyprshare-stream", nullptr);
  if (!impl_->thread_loop) {
    return std::unexpected("failed to create PipeWire thread loop");
  }

  impl_->context = pw_context_new(pw_thread_loop_get_loop(impl_->thread_loop), nullptr, 0);
  if (!impl_->context) {
    pw_thread_loop_destroy(impl_->thread_loop);
    impl_->thread_loop = nullptr;
    return std::unexpected("failed to create PipeWire context");
  }

  impl_->core = pw_context_connect(impl_->context, nullptr, 0);
  if (!impl_->core) {
    pw_context_destroy(impl_->context);
    pw_thread_loop_destroy(impl_->thread_loop);
    impl_->context = nullptr;
    impl_->thread_loop = nullptr;
    return std::unexpected("failed to connect to PipeWire");
  }

  // The thread loop must be pumping for stream events (including the buffer
  // `process` callback) to be dispatched.
  if (pw_thread_loop_start(impl_->thread_loop) < 0) {
    pw_core_disconnect(impl_->core);
    impl_->core = nullptr;
    pw_context_destroy(impl_->context);
    impl_->context = nullptr;
    pw_thread_loop_destroy(impl_->thread_loop);
    impl_->thread_loop = nullptr;
    return std::unexpected("failed to start PipeWire thread loop");
  }
  impl_->loop_running = true;

  return {};
}

std::expected<void, std::string> PipeWireStream::start() {
  if (!impl_->core) {
    return std::unexpected("PipeWire not connected");
  }

  if (impl_->stream) {
    // Stream already created and connected.
    return {};
  }

  if (impl_->node_id == 0) {
    return std::unexpected("no PipeWire node id supplied; cannot target a capture source");
  }

  // Passing a null format string is the documented way to create an empty
  // property set. GCC cannot see the variadic sentinel in that call and
  // reports it under -Wformat=, so the check is disabled for this line.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat="
#endif
  struct pw_properties* props = pw_properties_new(nullptr);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
  if (!props) {
    return std::unexpected("failed to create PipeWire stream properties");
  }
  if (impl_->node_id != 0) {
    pw_properties_set(props, PW_KEY_TARGET_OBJECT, std::to_string(impl_->node_id).c_str());
  }
  // Without the media type and role the source has no reason to send video.
  pw_properties_set(props, PW_KEY_MEDIA_TYPE, "Video");
  pw_properties_set(props, PW_KEY_MEDIA_CATEGORY, "Capture");
  pw_properties_set(props, PW_KEY_MEDIA_ROLE, "Screen");

  if (impl_->thread_loop) {
    pw_thread_loop_lock(impl_->thread_loop);
  }

  impl_->stream = pw_stream_new(impl_->core, "hyprshare-stream", props);
  if (!impl_->stream) {
    if (impl_->thread_loop) {
      pw_thread_loop_unlock(impl_->thread_loop);
    }
    pw_properties_free(props);
    return std::unexpected("failed to create PipeWire stream");
  }

  pw_stream_add_listener(impl_->stream, &impl_->listener, &impl_->events, impl_.get());

  const int rc = pw_stream_connect(
    impl_->stream, PW_DIRECTION_INPUT, PW_ID_ANY,
    PW_STREAM_FLAG_MAP_BUFFERS, nullptr, 0);

  if (impl_->thread_loop) {
    pw_thread_loop_unlock(impl_->thread_loop);
  }

  if (rc < 0) {
    pw_stream_destroy(impl_->stream);
    impl_->stream = nullptr;
    return std::unexpected("failed to connect PipeWire stream: " + std::string(strerror(rc)));
  }

  pw_stream_set_active(impl_->stream, true);
  impl_->started = true;
  return {};
}

std::expected<void, std::string> PipeWireStream::stop() {
  if (impl_->stream) {
    if (impl_->thread_loop) {
      pw_thread_loop_lock(impl_->thread_loop);
    }
    pw_stream_set_active(impl_->stream, false);
    if (impl_->thread_loop) {
      pw_thread_loop_unlock(impl_->thread_loop);
    }
  }
  impl_->started = false;
  return {};
}

void PipeWireStream::set_frame_callback(FrameCallback callback) {
  impl_->callback = std::move(callback);
}

bool PipeWireStream::is_connected() const {
  return impl_->connected;
}

std::optional<VideoFormat> PipeWireStream::video_format() const {
  if (!impl_->format_negotiated) {
    return std::nullopt;
  }
  return impl_->video_format;
}

void PipeWireStream::disconnect() {
  if (impl_->loop_running && impl_->thread_loop) {
    pw_thread_loop_stop(impl_->thread_loop);
    impl_->loop_running = false;
  }
  if (impl_->stream) {
    pw_stream_destroy(impl_->stream);
    impl_->stream = nullptr;
  }
  impl_->started = false;
  if (impl_->core) {
    pw_core_disconnect(impl_->core);
    impl_->core = nullptr;
  }
  if (impl_->context) {
    pw_context_destroy(impl_->context);
    impl_->context = nullptr;
  }
  if (impl_->thread_loop) {
    pw_thread_loop_destroy(impl_->thread_loop);
    impl_->thread_loop = nullptr;
  }
  impl_->connected = false;
  impl_->format_negotiated = false;
}

}  // namespace hyprshare::portal

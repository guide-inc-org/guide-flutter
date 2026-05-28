// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/windows/flutter_windows_texture_registrar.h"

#include <atomic>
#include <mutex>

#include "flutter/fml/logging.h"
#include "flutter/shell/platform/embedder/embedder_struct_macros.h"
#include "flutter/shell/platform/windows/external_texture_d3d.h"
#include "flutter/shell/platform/windows/external_texture_pixelbuffer.h"
#include "flutter/shell/platform/windows/flutter_windows_engine.h"

namespace {
static constexpr int64_t kInvalidTexture = -1;

// Software-mode texture ids start from a small positive number. They never
// collide with the GL/D3D path's ids because that path stores `this`
// pointers (large heap addresses) cast to int64.
static std::atomic<int64_t> next_software_texture_id{1};
}

namespace flutter {

FlutterWindowsTextureRegistrar::FlutterWindowsTextureRegistrar(
    FlutterWindowsEngine* engine,
    std::shared_ptr<egl::ProcTable> gl)
    : engine_(engine), gl_(std::move(gl)) {}

int64_t FlutterWindowsTextureRegistrar::RegisterTexture(
    const FlutterDesktopTextureInfo* texture_info) {
  if (texture_info->type == kFlutterDesktopPixelBufferTexture) {
    if (!texture_info->pixel_buffer_config.callback) {
      FML_LOG(ERROR) << "Invalid pixel buffer texture callback.";
      return kInvalidTexture;
    }

    // Pixel-buffer textures need to work in BOTH GL and software rendering
    // modes. |gl_| may be null if GL proc lookup failed, and even when it is
    // non-null it does not indicate which renderer mode is currently active.
    // Instead, register the callback in BOTH maps:
    // - |textures_| (via EmplaceTexture) for the GL path
    // - |software_pixel_buffer_callbacks_| for the software path
    // The actual rendering mode is selected by the engine later via the
    // appropriate FlutterRendererConfig callback.
    auto external_texture =
        std::make_unique<flutter::ExternalTexturePixelBuffer>(
            texture_info->pixel_buffer_config.callback,
            texture_info->pixel_buffer_config.user_data, gl_);
    int64_t texture_id = external_texture->texture_id();
    {
      std::lock_guard<std::mutex> lock(map_mutex_);
      software_pixel_buffer_callbacks_[texture_id] =
          texture_info->pixel_buffer_config;
    }
    return EmplaceTexture(std::move(external_texture));
  } else if (texture_info->type == kFlutterDesktopGpuSurfaceTexture) {
    if (!gl_) {
      // GPU surface textures require a GL context to share with - not
      // supported in software mode.
      return kInvalidTexture;
    }
    const FlutterDesktopGpuSurfaceTextureConfig* gpu_surface_config =
        &texture_info->gpu_surface_config;
    auto surface_type = SAFE_ACCESS(gpu_surface_config, type,
                                    kFlutterDesktopGpuSurfaceTypeNone);
    if (surface_type == kFlutterDesktopGpuSurfaceTypeDxgiSharedHandle ||
        surface_type == kFlutterDesktopGpuSurfaceTypeD3d11Texture2D) {
      auto callback = SAFE_ACCESS(gpu_surface_config, callback, nullptr);
      if (!callback) {
        FML_LOG(ERROR) << "Invalid GPU surface descriptor callback.";
        return kInvalidTexture;
      }

      auto user_data = SAFE_ACCESS(gpu_surface_config, user_data, nullptr);
      return EmplaceTexture(std::make_unique<flutter::ExternalTextureD3d>(
          surface_type, callback, user_data, engine_->egl_manager(), gl_));
    }
  }

  FML_LOG(ERROR) << "Attempted to register texture of unsupport type.";
  return kInvalidTexture;
}

int64_t FlutterWindowsTextureRegistrar::EmplaceTexture(
    std::unique_ptr<ExternalTexture> texture) {
  int64_t texture_id = texture->texture_id();
  {
    std::lock_guard<std::mutex> lock(map_mutex_);
    textures_[texture_id] = std::move(texture);
  }

  engine_->task_runner()->RunNowOrPostTask([engine = engine_, texture_id]() {
    engine->RegisterExternalTexture(texture_id);
  });

  return texture_id;
}

void FlutterWindowsTextureRegistrar::UnregisterTexture(int64_t texture_id,
                                                       fml::closure callback) {
  engine_->task_runner()->RunNowOrPostTask([engine = engine_, texture_id]() {
    engine->UnregisterExternalTexture(texture_id);
  });

  bool posted = engine_->PostRasterThreadTask([this, texture_id, callback]() {
    {
      std::lock_guard<std::mutex> lock(map_mutex_);
      auto it = textures_.find(texture_id);
      if (it != textures_.end()) {
        textures_.erase(it);
      }
      auto sw_it = software_pixel_buffer_callbacks_.find(texture_id);
      if (sw_it != software_pixel_buffer_callbacks_.end()) {
        software_pixel_buffer_callbacks_.erase(sw_it);
      }
    }
    if (callback) {
      callback();
    }
  });

  if (!posted && callback) {
    callback();
  }
}

bool FlutterWindowsTextureRegistrar::MarkTextureFrameAvailable(
    int64_t texture_id) {
  engine_->task_runner()->RunNowOrPostTask([engine = engine_, texture_id]() {
    engine->MarkExternalTextureFrameAvailable(texture_id);
  });
  return true;
}

bool FlutterWindowsTextureRegistrar::PopulateTexture(
    int64_t texture_id,
    size_t width,
    size_t height,
    FlutterOpenGLTexture* opengl_texture) {
  flutter::ExternalTexture* texture;
  {
    std::lock_guard<std::mutex> lock(map_mutex_);
    auto it = textures_.find(texture_id);
    if (it == textures_.end()) {
      return false;
    }
    texture = it->second.get();
  }
  return texture->PopulateTexture(width, height, opengl_texture);
}

bool FlutterWindowsTextureRegistrar::PopulateTextureSoftware(
    int64_t texture_id,
    size_t width,
    size_t height,
    FlutterSoftwarePixelBuffer* pixel_buffer) {
  FlutterDesktopPixelBufferTextureConfig cfg;
  {
    std::lock_guard<std::mutex> lock(map_mutex_);
    auto it = software_pixel_buffer_callbacks_.find(texture_id);
    if (it == software_pixel_buffer_callbacks_.end()) {
      return false;
    }
    cfg = it->second;
  }

  size_t w = width, h = height;
  const FlutterDesktopPixelBuffer* src = cfg.callback(w, h, cfg.user_data);
  if (!src || !src->buffer) {
    return false;
  }
  pixel_buffer->buffer = src->buffer;
  pixel_buffer->width = src->width;
  pixel_buffer->height = src->height;
  pixel_buffer->release_callback = src->release_callback;
  pixel_buffer->release_context = src->release_context;
  return true;
}

};  // namespace flutter

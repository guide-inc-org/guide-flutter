// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_EMBEDDER_EMBEDDER_EXTERNAL_TEXTURE_SOFTWARE_H_
#define FLUTTER_SHELL_PLATFORM_EMBEDDER_EMBEDDER_EXTERNAL_TEXTURE_SOFTWARE_H_

#include "flutter/common/graphics/texture.h"
#include "flutter/fml/macros.h"
#include "flutter/shell/platform/embedder/embedder.h"
#include "third_party/skia/include/core/SkSize.h"

namespace flutter {

// External texture implementation for software rendering mode.
// Wraps a CPU pixel buffer as a Skia raster image — no GL required.
// Mirrors EmbedderExternalTextureGL but bypasses the OpenGL pipeline so
// embedders can render external textures while Flutter is in software mode.
class EmbedderExternalTextureSoftware : public flutter::Texture {
 public:
  using ExternalTextureCallback = std::function<
      std::unique_ptr<FlutterSoftwarePixelBuffer>(int64_t, size_t, size_t)>;

  EmbedderExternalTextureSoftware(int64_t texture_identifier,
                                  const ExternalTextureCallback& callback);

  ~EmbedderExternalTextureSoftware() override;

 private:
  const ExternalTextureCallback& external_texture_callback_;
  sk_sp<DlImage> last_image_;

  sk_sp<DlImage> ResolveTexture(int64_t texture_id, const SkISize& size);

  // |flutter::Texture|
  void Paint(PaintContext& context,
             const SkRect& bounds,
             bool freeze,
             const DlImageSampling sampling) override;

  // |flutter::Texture|
  void OnGrContextCreated() override;

  // |flutter::Texture|
  void OnGrContextDestroyed() override;

  // |flutter::Texture|
  void MarkNewFrameAvailable() override;

  // |flutter::Texture|
  void OnTextureUnregistered() override;

  FML_DISALLOW_COPY_AND_ASSIGN(EmbedderExternalTextureSoftware);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_EMBEDDER_EMBEDDER_EXTERNAL_TEXTURE_SOFTWARE_H_

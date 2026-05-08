// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/embedder/embedder_external_texture_software.h"

#include "flutter/fml/logging.h"
#include "third_party/skia/include/core/SkAlphaType.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColorType.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/core/SkImageInfo.h"
#include "third_party/skia/include/core/SkPixmap.h"
#include "third_party/skia/include/core/SkSize.h"

namespace flutter {

EmbedderExternalTextureSoftware::EmbedderExternalTextureSoftware(
    int64_t texture_identifier,
    const ExternalTextureCallback& callback)
    : Texture(texture_identifier), external_texture_callback_(callback) {
  FML_DCHECK(external_texture_callback_);
}

EmbedderExternalTextureSoftware::~EmbedderExternalTextureSoftware() = default;

// |flutter::Texture|
void EmbedderExternalTextureSoftware::Paint(PaintContext& context,
                                            const SkRect& bounds,
                                            bool freeze,
                                            const DlImageSampling sampling) {
  if (last_image_ == nullptr) {
    last_image_ =
        ResolveTexture(Id(),                                           //
                       SkISize::Make(bounds.width(), bounds.height())  //
        );
  }

  DlCanvas* canvas = context.canvas;
  const DlPaint* paint = context.paint;

  if (last_image_) {
    SkRect image_bounds = SkRect::Make(last_image_->bounds());
    if (bounds != image_bounds) {
      canvas->DrawImageRect(last_image_, image_bounds, bounds, sampling, paint);
    } else {
      canvas->DrawImage(last_image_, SkPoint{bounds.x(), bounds.y()}, sampling,
                        paint);
    }
  }
}

sk_sp<DlImage> EmbedderExternalTextureSoftware::ResolveTexture(
    int64_t texture_id,
    const SkISize& size) {
  std::unique_ptr<FlutterSoftwarePixelBuffer> pixel_buffer =
      external_texture_callback_(texture_id, size.width(), size.height());

  if (!pixel_buffer || !pixel_buffer->buffer || pixel_buffer->width == 0 ||
      pixel_buffer->height == 0) {
    return nullptr;
  }

  SkImageInfo info = SkImageInfo::Make(
      static_cast<int>(pixel_buffer->width),
      static_cast<int>(pixel_buffer->height), kRGBA_8888_SkColorType,
      kPremul_SkAlphaType);
  SkPixmap pixmap(info, pixel_buffer->buffer,
                  pixel_buffer->width * 4 /* row bytes (RGBA) */);

  // Copy the pixels into a Skia-owned SkImage so we can release the source
  // buffer immediately. RasterFromPixmapCopy allocates and copies; for very
  // high frame rates we could alternatively use RasterFromPixmap with a
  // release proc to avoid the copy, but that requires more careful lifetime
  // management with the embedder's pixel buffer ownership model.
  sk_sp<SkImage> image = SkImages::RasterFromPixmapCopy(pixmap);

  if (pixel_buffer->release_callback) {
    pixel_buffer->release_callback(pixel_buffer->release_context);
  }

  if (!image) {
    FML_LOG(ERROR) << "Could not create software external texture image.";
    return nullptr;
  }

  return DlImage::Make(std::move(image));
}

// |flutter::Texture|
void EmbedderExternalTextureSoftware::OnGrContextCreated() {}

// |flutter::Texture|
void EmbedderExternalTextureSoftware::OnGrContextDestroyed() {}

// |flutter::Texture|
void EmbedderExternalTextureSoftware::MarkNewFrameAvailable() {
  last_image_ = nullptr;
}

// |flutter::Texture|
void EmbedderExternalTextureSoftware::OnTextureUnregistered() {}

}  // namespace flutter

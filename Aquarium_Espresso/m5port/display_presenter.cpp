#include "display_presenter.h"

#include <esp_heap_caps.h>
#include <math.h>

namespace {

static float clampZoom(float value) {
  if (!isfinite(value) || value <= 0.0f) return 1.0f;
  if (value < 0.10f) return 0.10f;
  if (value > 8.0f) return 8.0f;
  return value;
}

static inline uint8_t lerp8(uint8_t a, uint8_t b, float t) {
  return (uint8_t)(a + (b - a) * t + 0.5f);
}

static lgfx::rgb565_t sampleBilinear(const uint16_t* src, float sx, float sy) {
  if (sx < 0.0f || sy < 0.0f || sx > (float)(FB_W - 1) || sy > (float)(FB_H - 1)) {
    return lgfx::rgb565_t(0, 0, 0);
  }

  int x0 = (int)floorf(sx);
  int y0 = (int)floorf(sy);
  int x1 = x0 + 1;
  int y1 = y0 + 1;
  if (x1 >= FB_W) x1 = FB_W - 1;
  if (y1 >= FB_H) y1 = FB_H - 1;

  const float fx = sx - x0;
  const float fy = sy - y0;
  const lgfx::rgb565_t c00(src[y0 * FB_W + x0]);
  const lgfx::rgb565_t c10(src[y0 * FB_W + x1]);
  const lgfx::rgb565_t c01(src[y1 * FB_W + x0]);
  const lgfx::rgb565_t c11(src[y1 * FB_W + x1]);

  const uint8_t r0 = lerp8(c00.R8(), c10.R8(), fx);
  const uint8_t g0 = lerp8(c00.G8(), c10.G8(), fx);
  const uint8_t b0 = lerp8(c00.B8(), c10.B8(), fx);
  const uint8_t r1 = lerp8(c01.R8(), c11.R8(), fx);
  const uint8_t g1 = lerp8(c01.G8(), c11.G8(), fx);
  const uint8_t b1 = lerp8(c01.B8(), c11.B8(), fx);

  return lgfx::rgb565_t(lerp8(r0, r1, fy),
                        lerp8(g0, g1, fy),
                        lerp8(b0, b1, fy));
}

}  // namespace

AquariumDisplayPresenter::~AquariumDisplayPresenter() {
  end();
}

bool AquariumDisplayPresenter::begin(const AquariumDisplayConfig& config) {
  end();
  config_ = config;
  width_ = M5.Display.width();
  height_ = M5.Display.height();
  if (width_ <= 0 || height_ <= 0) return false;

  updateMapping();

  // 320x240 M5 devices (Core2/CoreS3/etc.) can present the canonical
  // framebuffer directly.  Do not allocate a second 153.6 KB frame just to
  // resample every pixel at scale 1.0.
  if (isIdentityMapping()) {
    Serial.println("presenter: identity fast path (no resample buffer)");
    return true;
  }

  const size_t count = (size_t)width_ * height_;
  const size_t bytes = count * sizeof(lgfx::rgb565_t);
  output_ = (lgfx::rgb565_t*)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!output_) {
    output_ = (lgfx::rgb565_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  if (!output_) {
    width_ = height_ = 0;
    return false;
  }

  Serial.printf("presenter: resample buffer %u bytes\n", (unsigned)bytes);
  return true;
}

void AquariumDisplayPresenter::end() {
  if (output_) {
    heap_caps_free(output_);
    output_ = nullptr;
  }
  width_ = height_ = 0;
}

void AquariumDisplayPresenter::setConfig(const AquariumDisplayConfig& config) {
  config_ = config;
  if (width_ > 0 && height_ > 0) updateMapping();
}

float AquariumDisplayPresenter::baseScale() const {
  if (width_ <= 0 || height_ <= 0) return 1.0f;
  const float sx = (float)width_ / FB_W;
  const float sy = (float)height_ / FB_H;
  return config_.fitMode == AquariumFitMode::Contain ? fminf(sx, sy) : fmaxf(sx, sy);
}

float AquariumDisplayPresenter::effectiveScale() const {
  return baseScale() * clampZoom(config_.zoomAdjust);
}

void AquariumDisplayPresenter::updateMapping() {
  scale_ = effectiveScale();
  const float spanW = width_ / scale_;
  const float spanH = height_ / scale_;
  sourceLeft_ = (FB_W - spanW) * 0.5f;
  sourceTop_ = (FB_H - spanH) * 0.5f;
}

bool AquariumDisplayPresenter::isIdentityMapping() const {
  constexpr float kTolerance = 0.0001f;
  return width_ == FB_W && height_ == FB_H
      && fabsf(scale_ - 1.0f) < kTolerance
      && fabsf(sourceLeft_) < kTolerance
      && fabsf(sourceTop_) < kTolerance;
}

AquariumViewportInfo AquariumDisplayPresenter::viewport() const {
  AquariumViewportInfo v{};
  v.scale = scale_;
  v.displayW = width_;
  v.displayH = height_;
  v.sourceW = width_ > 0 ? width_ / scale_ : 0.0f;
  v.sourceH = height_ > 0 ? height_ / scale_ : 0.0f;
  v.sourceX = sourceLeft_;
  v.sourceY = sourceTop_;
  return v;
}

bool AquariumDisplayPresenter::present(const uint16_t* framebuffer) {
  if (!framebuffer || width_ <= 0 || height_ <= 0) return false;

  if (isIdentityMapping()) {
    // FB is native, non-swapped RGB565.  Use M5GFX's typed overload so the
    // library handles the panel transport/color ordering without touching the
    // framebuffer.
    M5.Display.pushImage(0, 0, FB_W, FB_H,
                         reinterpret_cast<const lgfx::rgb565_t*>(framebuffer));
    return true;
  }

  if (!output_) return false;

  const float invScale = 1.0f / scale_;
  for (int y = 0; y < height_; ++y) {
    const float sy = sourceTop_ + (y + 0.5f) * invScale - 0.5f;
    for (int x = 0; x < width_; ++x) {
      const float sx = sourceLeft_ + (x + 0.5f) * invScale - 0.5f;
      output_[(size_t)y * width_ + x] = sampleBilinear(framebuffer, sx, sy);
    }
  }

  M5.Display.pushImage(0, 0, width_, height_, output_);
  return true;
}

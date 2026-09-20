#pragma once

#include <Arduino.h>
#include <M5Unified.h>

#include "gfx.h"

enum class AquariumFitMode : uint8_t {
  CoverCenter,
  Contain,
};

struct AquariumDisplayConfig {
  AquariumFitMode fitMode = AquariumFitMode::CoverCenter;
  float zoomAdjust = 1.0f;

  // Reserved renderer-level visual trim. 1.0 preserves upstream rendering.
  // Kept in runtime configuration now so device profiles/UI can expose it
  // without changing the public M5-port configuration later.
  float fishScaleAdjust = 1.0f;
};

struct AquariumViewportInfo {
  float sourceX;
  float sourceY;
  float sourceW;
  float sourceH;
  float scale;
  int displayW;
  int displayH;
};

class AquariumDisplayPresenter {
 public:
  AquariumDisplayPresenter() = default;
  ~AquariumDisplayPresenter();

  bool begin(const AquariumDisplayConfig& config = AquariumDisplayConfig{});
  void end();
  bool present(const uint16_t* framebuffer);

  void setConfig(const AquariumDisplayConfig& config);
  const AquariumDisplayConfig& config() const { return config_; }

  int width() const { return width_; }
  int height() const { return height_; }
  AquariumViewportInfo viewport() const;
  bool usesIdentityMapping() const { return isIdentityMapping(); }

 private:
  float baseScale() const;
  float effectiveScale() const;
  void updateMapping();
  bool isIdentityMapping() const;

  AquariumDisplayConfig config_{};
  int width_ = 0;
  int height_ = 0;
  float scale_ = 1.0f;
  float sourceLeft_ = 0.0f;
  float sourceTop_ = 0.0f;
  lgfx::rgb565_t* output_ = nullptr;
};

// M5Stack PlatformIO entry point for Aquarium_Espresso.
//
// The upstream simulation and renderer keep their canonical 320x240 world.
// M5-specific hardware setup and physical-display scaling live in this file
// and display_presenter.* so upstream behaviour stays easy to diff/merge.

#include <Arduino.h>
#include <M5Unified.h>
#include <esp_heap_caps.h>

#include "bg_images.h"
#include "bubbles.h"
#include "cardfish.h"
#include "fastmath.h"
#include "gfx.h"
#include "light.h"
#include "renderer.h"
#include "rig.h"
#include "sim.h"

#include "display_presenter.h"

#ifndef AQUARIUM_ENABLE_CARDFISH
#define AQUARIUM_ENABLE_CARDFISH 0
#endif

extern uint32_t tRestore[2], tVeil[2], tSeg[2], tExtra[2];

namespace {

Sim sim;
AquariumDisplayPresenter presenter;
AquariumDisplayConfig displayConfig;
float autoTap = 4.0f;

constexpr int BANDS = 5;
constexpr float SPLIT = 0.50f;

#if !defined(CONFIG_FREERTOS_UNICORE) || !CONFIG_FREERTOS_UNICORE
TaskHandle_t hWorker = nullptr;
TaskHandle_t hMain = nullptr;
const Sim* workerSim = nullptr;
volatile int workerY0 = 0;
volatile int workerY1 = 0;

void renderWorker(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    renderBand(*workerSim, workerY0, workerY1);
    xTaskNotifyGive(hMain);
  }
}
#endif

void renderBandPortable(const Sim& state, int y0, int y1) {
#if !defined(CONFIG_FREERTOS_UNICORE) || !CONFIG_FREERTOS_UNICORE
  if (hWorker) {
    const int ym = y0 + (int)((y1 - y0) * SPLIT + 0.5f);
    workerSim = &state;
    workerY0 = y0;
    workerY1 = ym;
    xTaskNotifyGive(hWorker);
    renderBand(state, ym, y1);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    return;
  }
#endif
  renderBand(state, y0, y1);
}

[[noreturn]] void fatal(const char* message) {
  Serial.printf("FATAL: %s\n", message);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_RED, TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(2, 4);
  M5.Display.println("Aquarium error");
  M5.Display.setCursor(2, 18);
  M5.Display.println(message);
  for (;;) delay(1000);
}

bool loadBackdrop() {
  const size_t bytes = (size_t)FB_W * FB_H * sizeof(uint16_t);
  BGBUF = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!BGBUF) return false;

  M5Canvas sprite(&M5.Display);
  sprite.setPsram(true);
  sprite.setColorDepth(16);
  if (!sprite.createSprite(FB_W, FB_H)) return false;

  const int pick = (int)(esp_random() % N_BG_IMAGES);
  sprite.fillScreen(TFT_BLACK);
  const bool ok = sprite.drawJpg(BG_IMAGES[pick].data, BG_IMAGES[pick].len, 0, 0);
  if (ok) {
    // Explicit M5GFX RGB565 type avoids depending on sprite/panel byte order.
    sprite.readRect(0, 0, FB_W, FB_H, (lgfx::rgb565_t*)BGBUF);
  }
  sprite.deleteSprite();

  Serial.printf("backdrop %d/%d (%u B): %s\n", pick + 1, N_BG_IMAGES,
                (unsigned)BG_IMAGES[pick].len, ok ? "ok" : "FAILED");
  return ok;
}

void initRendererWorker() {
#if !defined(CONFIG_FREERTOS_UNICORE) || !CONFIG_FREERTOS_UNICORE
  hMain = xTaskGetCurrentTaskHandle();
  const BaseType_t currentCore = xPortGetCoreID();
  const BaseType_t workerCore = currentCore == 0 ? 1 : 0;
  const BaseType_t result = xTaskCreatePinnedToCore(
      renderWorker, "aquarium_render", 8192, nullptr, 2, &hWorker, workerCore);
  if (result != pdPASS) hWorker = nullptr;
  Serial.printf("renderer: %s\n", hWorker ? "dual-core" : "single-core fallback");
#else
  Serial.println("renderer: single-core");
#endif
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nAquarium_Espresso / M5Stack port");

  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setColorDepth(16);
  M5.Display.fillScreen(TFT_BLACK);

  Serial.printf("M5 board=%d display=%dx%d\n", (int)M5.getBoard(),
                M5.Display.width(), M5.Display.height());

  fastMathInit();

  const size_t fbBytes = (size_t)FB_W * FB_H * sizeof(uint16_t);
  FB = (uint16_t*)heap_caps_malloc(
      fbBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  if (!FB) {
    FB = (uint16_t*)heap_caps_malloc(fbBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    Serial.println("framebuffer: PSRAM fallback");
  }
  if (!FB) fatal("no canonical framebuffer");

  if (!loadBackdrop()) fatal("backdrop decode failed");

  lightInit();
  lightPrepBackdrop();
  bubblesInit();

#if AQUARIUM_ENABLE_CARDFISH
  if (cardLoad()) {
    CARDFISH.count = CARD_MIN + (int)(esp_random() % (CARD_MAX - CARD_MIN + 1));
  }
#else
  Serial.println("cardfish: disabled by build option");
#endif

  if (!buildRigs()) fatal("sprite rig build failed");
  makeSim(sim);

  displayConfig.fitMode = AquariumFitMode::CoverCenter;
  displayConfig.zoomAdjust = 1.0f;
  displayConfig.fishScaleAdjust = 1.0f;
  if (!presenter.begin(displayConfig)) fatal("display presenter alloc failed");

  const AquariumViewportInfo vp = presenter.viewport();
  Serial.printf("present: canonical %dx%d -> LCD %dx%d, scale %.5f, source %.2f,%.2f %.2fx%.2f\n",
                FB_W, FB_H, vp.displayW, vp.displayH, vp.scale,
                vp.sourceX, vp.sourceY, vp.sourceW, vp.sourceH);

  initRendererWorker();
}

void loop() {
  static uint32_t last = micros();
  static uint32_t fpsTime = millis();
  static int frames = 0;

  M5.update();

  const uint32_t now = micros();
  float dt = (now - last) / 1000000.0f;
  last = now;
  if (dt > 1.0f / 30.0f) dt = 1.0f / 30.0f;

  autoTap -= dt;
  if (autoTap <= 0.0f) {
    autoTap = 12.0f + (float)esp_random() / 4294967296.0f * 20.0f;
    const float tx = 20.0f + (float)esp_random() / 4294967296.0f * 280.0f;
    const float ty = VIEW::y0 +
        (float)esp_random() / 4294967296.0f * (VIEW::y1 - VIEW::y0);
    tapWater(sim, tx, ty);
  }

  bubblesStep(dt);
  lightStep(dt);
  stepSim(sim, dt);

  for (int band = 0; band < BANDS; ++band) {
    const int y0 = band * SCR_H / BANDS;
    const int y1 = (band + 1) * SCR_H / BANDS;
    renderBandPortable(sim, y0, y1);
  }

  presenter.present(FB);
  ++frames;

  if (millis() - fpsTime >= 5000) {
    const uint32_t elapsed = millis() - fpsTime;
    Serial.printf("%.1f fps\n", frames * 1000.0f / elapsed);
    tRestore[0] = tRestore[1] = tVeil[0] = tVeil[1] = 0;
    tSeg[0] = tSeg[1] = tExtra[0] = tExtra[1] = 0;
    fpsTime = millis();
    frames = 0;
  }
}

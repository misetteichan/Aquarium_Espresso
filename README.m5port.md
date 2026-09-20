# Aquarium_Espresso M5Stack port

This branch keeps the original Aquarium_Espresso simulation, renderer, fish art,
lighting, bubbles, and 320x240 canonical framebuffer as intact as possible while
adding a thin PlatformIO + M5Unified presentation layer for M5Stack displays.

## Design

The upstream `Aquarium_Espresso/` sources remain the authority. M5-specific code
lives in `Aquarium_Espresso/m5port/`. There is no build-time upstream download,
no `generated/upstream`, and no device-specific copy of `fish_art.cpp` or
`bg_images.cpp`.

Rendering stays at the original 320x240 resolution:

```
original simulation -> original renderer -> 320x240 RGB565 framebuffer
                                           -> M5 display presenter -> LCD
```

After `M5.begin()`, the presenter reads `M5.Display.width()` and
`M5.Display.height()` and computes the mapping at runtime. The default mode is
`CoverCenter`:

```
scale = max(display_width / 320, display_height / 240)
```

For an AtomS3R (128x128), that gives a centered 240x240 source viewport and a
scale of about 0.533333. The whole finished scene is resampled, so fish,
background, bubbles, lighting and renderer effects keep their original relative
scale.

`Contain` is also represented by the presenter and can be selected at runtime;
it letterboxes/pillarboxes when necessary.

## Runtime tuning

`AquariumDisplayConfig` currently contains:

- `fitMode` (`CoverCenter` by default)
- `zoomAdjust` (`1.0` by default; active at runtime)
- `fishScaleAdjust` (`1.0` by default)

`fishScaleAdjust` is intentionally reserved at `1.0` in the first port. A true
fish-only scale needs a renderer-level transform; applying it in the final
frame scaler would also scale the background. Keeping the field now makes the
configuration ABI ready without introducing a large upstream renderer diff.
When implemented, it should be a minimal renderer hook and must leave simulation
coordinates/behaviour unchanged.

## Card fish / SD

The original card-fish source remains in the tree. The M5 port controls whether
`cardLoad()` is invoked with `AQUARIUM_ENABLE_CARDFISH`. It is off by default for
AtomS3R, so no SD initialization occurs. The original card-fish implementation
is not deleted or rewritten.

## PlatformIO

Build the current target with:

```sh
pio run -e atoms3r
```

Upload with:

```sh
pio run -e atoms3r -t upload
```

The AtomS3R board JSON is checked in under `boards/` because the selected
PlatformIO Espressif32 platform version does not necessarily ship an AtomS3R
board definition. The file follows M5Stack's official AtomS3R PlatformIO board
definition.

To add another M5Stack LCD device, add a PlatformIO environment/board definition
for its MCU as needed. Do not add display dimensions to `sim.cpp` or
`renderer.cpp`; the runtime presenter obtains them from M5Unified.

## Upstream-preservation policy

Avoid formatting or moving upstream files. M5-specific hardware/display code
belongs in `Aquarium_Espresso/m5port/`. Changes to upstream files should be
limited to small semantic-preserving compiler/build compatibility fixes, so
`git diff upstream/main..m5port` remains useful.

At present the required upstream compatibility fixes are:

- `sim.h`: make `SWIM_TOP` and `SWIM_BOT` `constexpr` because `ymap()` is
  `constexpr` under the PlatformIO toolchain.
- `renderer.h`: stop including the original fixed-panel `lgfx_setup.h`; the
  renderer itself does not use the `LGFX` panel class, and M5 hardware setup is
  owned by M5Unified in the port entry point.

Original project: `mochimochi-man/Aquarium_Espresso` (MIT licensed).

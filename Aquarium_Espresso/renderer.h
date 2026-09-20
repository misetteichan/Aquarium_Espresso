#ifndef PIXAQ_RENDERER_H
#define PIXAQ_RENDERER_H
// ---------------------------------------------------------------------------
// renderer.h — bent-strip fish renderer + tank environment.
// ---------------------------------------------------------------------------
#include "sim.h"
#include "gfx.h"

// compose rows [y0,y1) of the frame into FB
void renderBand(const Sim& sim, int y0, int y1);

#endif // PIXAQ_RENDERER_H

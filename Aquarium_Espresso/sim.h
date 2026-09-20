#ifndef PIXAQ_SIM_H
#define PIXAQ_SIM_H
// ---------------------------------------------------------------------------
// sim.h — fish behaviour, spine chain, depth (toward/away) events.
// Direct port of the browser build; the 320x200 coordinate space is unchanged.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include "rig.h"

namespace VIEW {
  // The browser reserved the lower 40% of its 200px canvas for the floor and
  // the depth rail; the tank photo replaces both, so every y from the web build
  // is remapped onto the panel through ymap(). Move SWIM_TOP / SWIM_BOT to
  // re-frame how much of the photo the fish roam over - nothing else needs to
  // change.
  static constexpr float SWIM_TOP = 30.0f;
  static constexpr float SWIM_BOT = 205.0f;
  static constexpr float ymap(float v) {
    return SWIM_TOP + (v - 26.0f) * ((SWIM_BOT - SWIM_TOP) / 94.0f);
  }

  static const float x0 = 14, x1 = 306;          // swim bounds x
  static const float y0 = ymap(26), y1 = ymap(120);
  static const float horizonY = ymap(18);
  static const float floorY   = ymap(134);
  static const int   W = 320, H = 240;
}

struct DepthEv {
  uint8_t mode;          // 0 idle, 1 event
  float t, dur;
  float from, to;
  float sign;            // -1 toward viewer, +1 away
  float cool;
  float bell;            // transient envelope 0..1
};

// distance-based motion history (one point per ~0.45px swum)
struct Trail {
  static const int CAP = 128;
  float x[CAP], y[CAP];
  int   n, head;         // head = newest
  void reset() { n = 0; head = CAP - 1; }
  void push(float px, float py) {
    head = (head + 1) % CAP;
    x[head] = px; y[head] = py;
    if (n < CAP) n++;
  }
  // back = 0 is the newest sample
  int idx(int back) const { return (head - back + CAP * 2) % CAP; }
};

struct Bone { float x, y, a; };

// Individual habits. The browser build gives every fish the same wandering
// rule, which reads as a screensaver; a tank reads as alive because a few
// individuals are always doing something of their own.
enum Personality : uint8_t {
  PERS_NONE = 0,   // just swims
  PERS_GULPER,     // rises to the surface now and then and gulps at it
  PERS_LOAFER,     // parks against a wall or on the bottom and sits there
  PERS_HOVERER,    // holds station in open water for long stretches
};

enum FishAct : uint8_t {
  ACT_SWIM = 0,
  ACT_RISE, ACT_GULP, ACT_SINK,     // gulper
  ACT_PARK, ACT_HOVER,              // loafer
  ACT_HOLD,                         // hoverer: parked in mid-water
  // corydoras: works the bottom, bolts up for air, settles back down
  ACT_GRAZE, ACT_REST, ACT_DASH, ACT_AIR, ACT_SETTLE,
  // amano shrimp: picks at one spot, scoots to the next, swims when it wants
  // to be elsewhere, and shoots backwards when something startles it
  ACT_PICK, ACT_CRAWL, ACT_SWIMOFF, ACT_FLICK,
};

struct Fish {
  const SpeciesCfg* cfg;
  const SpeciesCfg* home;   // what it really is, while cfg says otherwise
  float x, y;
  float heading;
  float speed;
  float prevHeading;
  float turnRate;        // smoothed rad/s
  float tx, ty;          // steering target
  float retarget;
  Trail trail;
  float beat;
  float phase;
  float sf;              // depth scale factor (persists between events)
  DepthEv depth;
  float biasF;           // smoothed roll bias (rows)
  float burstX, burstY;
  float sepX, sepY;      // crowd separation, recomputed every frame
  float speedNorm;
  bool    school;
  uint8_t sid;           // which shoal, when school is true
  float   orbitR;
  uint8_t pers;          // Personality
  uint8_t act;           // FishAct
  float   actT;          // time left in the current act
  float   nextAct;       // countdown to the next one
  float   holdX, holdY;  // where this act is taking it
  float   effort;        // swim effort multiplier, smoothed
  float   lifted;        // seconds of slack left on the layer clamp after the
                         // air stone has carried it out of its own water
  float   thrash;        // 0..1 whole-body writhing, for the corydoras dash
  Bone  bones[BONES];    // per-frame render cache
  float facing;          // 1 = pure side view
  float mirror;          // +1 art as drawn (heading right), -1 flipped
  float turn;            // card fish only: rotation about its own vertical
                         // axis. 0 = facing right, PI = facing left, and the
                         // values in between are the card edge-on.
  float flare;           // fin billow, 0 cruising .. 1.5 mid-turn
};

struct Surge  { float t, dur, dir, mag; };
struct Mote   { float x, y, vx, vy, a, ph; };
struct Bubble { float x, y, r, vy, ph, a; };
struct School { float x, y, tx, ty, timer, dash; float home, homeY; };

// Three shoals instead of one. Twenty tetras orbiting a single centre pack
// into one blob however wide the orbit gets; splitting them into separate
// shoals with their own home stretch of the tank is what actually spreads
// them out.
static const int N_SCHOOLS = 3;
// A shrimp day is not a full tank with shrimp added to it. It is a different
// tank: the shoal is down to five fish and there are five Amano working the
// sand instead. That gap is the point - twenty neons fill the middle of the
// water and you never look past them, and five do not, so on a shrimp day you
// notice the bottom is busy before you notice what is on it.
//
// So the stocking is a runtime number (`Sim::n`) and this is only the size of
// the array: the biggest the tank ever gets, which is a day without shrimp.
static const int N_FISH    = 20 + 8 + 3 + 2;   // neon, guppy, black, cory
static const int N_NEON_SHRIMPDAY = 5;

// How many copies of the picture on the SD card join the tank, when there is
// one. More than one reads as a shoal of the same drawing, which is exactly
// what the aquariums this imitates looked like.
//
// They are not extra fish. Each one takes a guppy's place - same water, same
// size, same way of swimming, so the tank stays the density it was designed
// at and the card cannot slowly silt it up.
static const int CARD_MIN = 1, CARD_MAX = 3;
static const int N_GUPPY = 8;                  // 2+2+2+1+1 across the strains

// How quickly the card's face catches up with the direction it is travelling.
// This is only there to take the jitter out of the heading - it is not what
// paces the turn, and it must not be slow enough to become that.
static const float CARD_FACE_EASE = 5.0f;

// How flat the card lies when it is not going straight sideways. The face
// follows the heading, so a card swimming at 45 degrees would be down to 71%
// of its width on the raw cosine; this pulls it back out to 84%, and keeps the
// truly edge-on moment for the reversal itself where it belongs.
static const float CARD_FACE_FLAT = 0.5f;
static const int N_MOTES   = 22;
static const int MAX_BUB   = 10;
static const int MAX_SURGE = 6;

struct Sim {
  Fish   fish[N_FISH];
  int    n;              // how many of them this boot actually stocked
  School school[N_SCHOOLS];
  Surge  surges[MAX_SURGE];
  int    nSurge;
  Bubble bubbles[MAX_BUB];
  int    nBub;
  Mote   motes[N_MOTES];
  float  stress;         // 0..1 global excitement
  float  nextBubble;
  float  t;
  float  tw;             // t wrapped to a common period, for the trig calls
  bool   ebiDay;         // this boot's guppies are ebi-fry
  bool   shrimpDay;      // this boot's tank came with shrimp in it
  float  sway;           // horizontal water displacement, px
  float  swayV;
};

// Every multiplier applied to `t`/`beat` before sinf() is a multiple of 0.01,
// so wrapping at 200*2pi keeps each wave continuous while keeping the argument
// small - large arguments make newlib's sinf argument reduction dominate the
// per-pixel cost.
static const float TRIG_WRAP = 200.0f * 2.0f * (float)M_PI;

// Once in a while - about one power-up in thirty - the tank comes up with
// every guppy already fried, and stays that way for the whole session. The
// coin is tossed at boot and never again: a tank that started out normal stays
// normal, so nothing ever changes under you while you are watching.
static const float GAG_CHANCE = 0.03f;

// And a second, much less silly coin, tossed at the same moment and for the
// same reason: a tank either has Amano shrimp in it or it does not, and which
// one you got should be settled before you start watching. About one boot in
// ten comes up with them.
static const float SHRIMP_CHANCE = 0.10f;

void makeSim(Sim& sim);
void stepSim(Sim& sim, float dt);
void tapWater(Sim& sim, float x, float y);
void startDepthEvent(Fish& f, bool stress);

// walk the motion trail backwards by `back` px
void trailAt(const Trail& tr, float back, float& ox, float& oy);

#endif // PIXAQ_SIM_H

// world.h - procedural low-poly town: blocks of rectangular buildings, two
// rivers with bridges, quays, trees, clouds. Everything is axis-aligned boxes
// so collision is a flat list of 2D AABBs (you can never get inside a building).
#pragma once
#include <cstdint>
#include <vector>
#include "math3d.h"

// Interleaved static vertex: position, normal, colour, window params.
//   win  - how strongly the procedural window grid shows on side faces
//   glow - per-building random seed used for "lit window" variation at dusk
struct Vertex
{
    float x, y, z;
    float nx, ny, nz;
    float r, g, b;
    float win, glow;
};

struct CloudVertex
{
    float x, y, z;
    float r, g, b, a;
};

struct AABB { float minX, minZ, maxX, maxZ; };

// --- town layout constants (shared with the shader for the cloud wrap) ------
static const int   TW_GRID       = 13;      // city blocks per side
static const float TW_BLOCK      = 42.0f;   // block (lot) size
static const float TW_ROAD        = 14.0f;  // street corridor width
static const float TW_CELL        = TW_BLOCK + TW_ROAD;
static const float TW_CITY_HALF   = TW_GRID * TW_CELL * 0.5f;   // 364 m
// Walkable surfaces are NOT all at one height. Anything standing on the ground
// must ask World::surfaceAt(x,z): one shared "ground level" constant is exactly
// what let walkers sink into the road and wade over bridge decks.
static const float TW_FIELD_Y      = -0.04f; // terrain apron outside the grid
static const float TW_LOT_Y        = 0.0f;   // block paving, grass
static const float TW_STREET_Y     = 0.05f; // asphalt, raised to avoid z-fighting
static const float TW_DECK_Y       = 0.12f; // bridge deck top
static const float TW_QUAY_Y       = 0.14f; // river kerb / coping top
static const float TW_QUAY_W       = 1.05f; // kerb width, both rivers
static const float TW_RAIL_T       = 0.55f; // bridge railing thickness
static const float TW_WATER_Y     = -1.55f; // river surface
static const float TW_RIVERBED_Y  = -1.95f;
static const float TW_TERRAIN     = 1400.0f;// outer terrain apron half-size
static const float TW_RIVER_LEN   = TW_TERRAIN; // rivers run to the fog line
static const int   TW_RIVER_V     = 8;      // vertical river: street corridor #8
static const int   TW_RIVER_H     = 5;      // horizontal river: street corridor #5
static const float TW_RIVER_VH    = 11.0f;  // half width, vertical river
static const float TW_RIVER_HH    = 14.0f;  // half width, horizontal river
static const float TW_PLAYER_R    = 0.45f;  // collision radius
static const float TW_WALK_LIMIT  = 1050.0f;
static const float TW_CLOUD_RANGE = 1200.0f;

struct World
{
    std::vector<Vertex>      solid;   // buildings, ground, bridges, quays, props
    std::vector<Vertex>      water;   // river surfaces (alpha blended)
    std::vector<CloudVertex> clouds;

    std::vector<AABB> blockers;       // buildings / trees / railings
    std::vector<AABB> waterRects;     // rivers: not walkable
    std::vector<AABB> bridgeZones;    // exceptions to waterRects

    vec3  spawn     = vec3(0, 0, 0);
    float spawnYaw  = 0.0f;

    void build(uint32_t seed);

    // True if a player circle of radius TW_PLAYER_R at (x,z) overlaps a wall,
    // a prop, the water (unless standing on a bridge) or the world border.
    bool blockedAt(float x, float z) const;

    // Height of the walkable surface under a point: fields, block paving,
    // asphalt, river kerbs and bridge decks each sit at their own height.
    // Deck wins over kerb, because a deck crosses the kerbs it passes through.
    float surfaceAt(float x, float z) const;
};

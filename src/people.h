// people.h - pedestrians.
//
// Each walker is routed on a graph of the street lattice (one node per street
// intersection). Because the rivers occupy whole street corridors and the only
// walkable nodes inside a river corridor are the bridge decks, a path that
// reaches the far bank *has* to go over a bridge - crossings are a property of
// the routing, not a special case.
//
// Bodies are solid: walkers steer around one another and around the player, and
// a hard non-overlap pass afterwards guarantees nobody ever shares space with
// anybody else. The player is stopped by them (see People::blocks).
//
// Geometry is six boxes - head, hair slab, torso, two arms, two legs - swinging
// about the hip and shoulder, Minecraft fashion.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "math3d.h"
#include "world.h"

struct Person
{
    vec3  pos   = vec3(0, 0, 0);   // at the feet
    vec3  vel   = vec3(0, 0, 0);
    float yaw   = 0.0f;             // facing; forward = (sin yaw, 0, -cos yaw)
    float speed = 1.35f;            // m/s, a comfortable stroll
    float phase = 0.0f;             // walk cycle, radians
    float radius = 0.30f;           // body radius for ped/ped + ped/player spacing

    std::vector<int> path;          // node indices still to walk
    size_t at   = 0;                // index into path
    int    dest = -1;               // destination node (-1 = none yet)
    float  wait = 0.0f;             // idle time left at a destination
    float  idleT = 0.0f;            // watchdog: seconds without real progress
    vec3   anchor = vec3(0, 0, 0);  // position the watchdog measures from

    int skin = 0, hair = 0, shirt = 0, legs = 0, shoes = 0;   // palette indices

    float sideV = 0.0f;             // which side of the vertical river (±1)
    float sideH = 0.0f;
    int   crossings = 0;            // completed river crossings, all of them by bridge
};

struct People
{
    struct Stats
    {
        int total = 0, walking = 0, waiting = 0;
        int overlaps = 0;    // body pairs inside each other (must stay 0)
        int inSolid  = 0;    // standing in a wall (must stay 0)
        int inWater  = 0;    // in a river off a bridge (must stay 0)
        int onBridge = 0;
        int crossings = 0;   // total crossings since build
    };

    std::vector<Person> peds;
    int nodes = 0;             // TW_GRID * TW_GRID intersections
    int edges = 0;             // undirected street links
    int walkableNodes = 0;     // nodes not in open water

    void build(const World& w, uint32_t seed, int count);
    void update(const World& w, float dt, const vec3& playerPos);
    void emit(std::vector<Vertex>& out) const;    // appends the bodies

    // The player must not be able to walk through anyone.
    bool blocks(const vec3& p, float r) const;
    void pushOut(vec3& p, float r) const;

    Stats stats(const World& w) const;
    void simulate(const World& w, float seconds, const vec3& playerStill);

    // --- routing graph -------------------------------------------------------
    vec3 nodePos(int n) const;
    int  nodeOf(float x, float z) const;
    bool plan(int from, int to, std::vector<int>& outPath);   // BFS
    int  hops(int from, int to);                              // -1 if unreachable

    // --- state (graph + generator) ------------------------------------------
    std::vector<unsigned char> walk;    // 1 = node is on footway/bridge
    std::vector<int> nbr;               // 4 slots per node: +x, -x, +z, -z, -1 = none
    std::vector<int> prev;              // BFS scratch
    std::vector<unsigned char> seen;    // BFS scratch
    uint32_t rngState = 1;

    // --- broad phase: uniform grid over the town, rebuilt every update -------
    // Without it, avoiding one another costs O(n^2) per frame and a town-sized
    // crowd stops paying for itself.
    std::vector<int> cellHead;
    std::vector<int> cellNext;
    int   gridAxis = 0;
    float gridMin  = 0.0f;
    float gridCell = 4.0f;
    int  cellIndex(float x, float z) const;
    void buildGrid();

    uint32_t rnd();
    int  irange(int lo, int hi);
};

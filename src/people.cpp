// people.cpp - see people.h for the design.
#include "people.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

// Centre-line of street corridor #i - the same formula World::build uses to lay
// the streets out, so a node always sits in the middle of a walkable street.
inline float corridorCentre(int i) { return -TW_CITY_HALF + (float)i * TW_CELL + TW_ROAD * 0.5f; }

// River centre-lines (World::build puts the river *in* these corridors).
const float kRiverVX = -TW_CITY_HALF + (float)TW_RIVER_V * TW_CELL + TW_ROAD * 0.5f;
const float kRiverHZ = -TW_CITY_HALF + (float)TW_RIVER_H * TW_CELL + TW_ROAD * 0.5f;

// Bridges are only ever built on even corridors, and never on the corridor that
// holds the other river - mirrored here so routing agrees with the geometry.
inline bool bridgeAlongX(int j) { return (j % 2) == 0; }                       // spans the vertical river
inline bool bridgeAlongZ(int i) { return (i % 2) == 0 && i != TW_RIVER_V; }    // spans the horizontal river

// --- blocky-body palettes ----------------------------------------------------
const vec3 kSkin[5] = {
    vec3(0.87f, 0.68f, 0.55f), vec3(0.76f, 0.55f, 0.42f), vec3(0.60f, 0.42f, 0.31f),
    vec3(0.42f, 0.29f, 0.21f), vec3(0.93f, 0.79f, 0.68f),
};
const vec3 kHair[4] = {
    vec3(0.16f, 0.13f, 0.11f), vec3(0.35f, 0.22f, 0.13f), vec3(0.62f, 0.52f, 0.35f),
    vec3(0.24f, 0.24f, 0.27f),
};
const vec3 kShirt[8] = {
    vec3(0.36f, 0.44f, 0.62f), vec3(0.62f, 0.30f, 0.28f), vec3(0.34f, 0.52f, 0.38f),
    vec3(0.72f, 0.66f, 0.55f), vec3(0.45f, 0.38f, 0.55f), vec3(0.28f, 0.42f, 0.47f),
    vec3(0.75f, 0.55f, 0.30f), vec3(0.52f, 0.52f, 0.55f),
};
const vec3 kTrousers[5] = {
    vec3(0.25f, 0.26f, 0.32f), vec3(0.34f, 0.30f, 0.26f), vec3(0.20f, 0.20f, 0.22f),
    vec3(0.42f, 0.40f, 0.36f), vec3(0.30f, 0.24f, 0.22f),
};
const vec3 kShoes[3] = { vec3(0.14f, 0.13f, 0.13f), vec3(0.30f, 0.28f, 0.26f), vec3(0.20f, 0.22f, 0.26f) };

// --- one rotated, yaw-oriented box -----------------------------------------
// Local frame: +y up, +z forward (the way the walker faces), +x to the right.
// A limb is described by its pivot (hip/shoulder), its centre and its half
// extents, and swings about the local x axis through that pivot.
void addBox(std::vector<Vertex>& v, const vec3& feet, float yaw,
            const vec3& pivot, const vec3& centre, const vec3& half,
            float swing, const vec3& col)
{
    const float cs = std::cos(swing), ss = std::sin(swing);
    const float cy = std::cos(yaw),   sy = std::sin(yaw);

    // local -> world: swing about x (through pivot), then yaw about y, then feet.
    auto toWorld = [&](const vec3& local) {
        const vec3 o = local - pivot;                       // relative to the hinge
        const float y = o.y * cs - o.z * ss;                // swing
        const float z = o.y * ss + o.z * cs;
        return vec3(feet.x + o.x * cy + z * sy,
                    feet.y + y,
                    feet.z + o.x * sy - z * cy);
    };

    const vec3 c[8] = {
        toWorld(centre + vec3(-half.x, -half.y, -half.z)),
        toWorld(centre + vec3( half.x, -half.y, -half.z)),
        toWorld(centre + vec3( half.x,  half.y, -half.z)),
        toWorld(centre + vec3(-half.x,  half.y, -half.z)),
        toWorld(centre + vec3(-half.x, -half.y,  half.z)),
        toWorld(centre + vec3( half.x, -half.y,  half.z)),
        toWorld(centre + vec3( half.x,  half.y,  half.z)),
        toWorld(centre + vec3(-half.x,  half.y,  half.z)),
    };
    static const int F[6][4] = { {0,1,2,3}, {5,4,7,6}, {4,0,3,7}, {1,5,6,2}, {3,2,6,7}, {4,5,1,0} };
    const vec3 mid = centre;                                // box centre, for facing test
    const vec3 wc = toWorld(mid);

    for (int f = 0; f < 6; ++f)
    {
        const vec3& a = c[F[f][0]]; const vec3& b = c[F[f][1]];
        const vec3& d = c[F[f][2]]; const vec3& e = c[F[f][3]];
        vec3 n = cross(b - a, d - a);
        const vec3 faceMid((a.x + d.x) * 0.5f, (a.y + d.y) * 0.5f, (a.z + d.z) * 0.5f);
        vec3 outward = faceMid - wc;
        if (dot(n, outward) < 0.0f) { n = n * -1.0f; }      // normal must leave the box
        const float nl = length(n) > 1e-6f ? length(n) : 1.0f;
        n = n * (1.0f / nl);
        // winding: order the four corners so the face is front-on to that normal
        const vec3* o[4] = { &a, &b, &d, &e };
        if (dot(cross(b - a, d - a), outward) < 0.0f) { o[1] = &e; o[2] = &d; o[3] = &b; }
        for (int t = 0; t < 6; ++t)
        {
            const vec3& p = *o[t == 0 ? 0 : (t == 1 ? 1 : (t == 2 ? 2 : (t == 3 ? 0 : (t == 4 ? 2 : 3))))];
            Vertex vv;
            vv.x = p.x; vv.y = p.y; vv.z = p.z;
            vv.nx = n.x; vv.ny = n.y; vv.nz = n.z;
            vv.r = col.x; vv.g = col.y; vv.b = col.z;
            vv.win = 0.0f;                                  // skin and cloth, no windows
            vv.glow = 0.0f;
            v.push_back(vv);
        }
    }
}
// --- proportions (about 1.79 m overall, blocky rather than anatomical) -----
const vec3 kLegPivotL(-0.115f, 0.72f, 0.0f);
const vec3 kLegPivotR( 0.115f, 0.72f, 0.0f);
const vec3 kArmPivotL(-0.255f, 1.30f, 0.0f);
const vec3 kArmPivotR( 0.255f, 1.30f, 0.0f);

} // namespace

// ---------------------------------------------------------------------------
// geometry
// ---------------------------------------------------------------------------
void People::emit(std::vector<Vertex>& out) const
{
    for (size_t i = 0; i < peds.size(); ++i)
    {
        const Person& p = peds[i];
        const float s    = std::sin(p.phase);
        const vec3 feet(p.pos.x, p.pos.y + 0.03f * std::fabs(s), p.pos.z);   // walk bob
        const vec3 skin  = kSkin[p.skin    % (int)(sizeof(kSkin)/sizeof(vec3))];
        const vec3 hair  = kHair[p.hair    % (int)(sizeof(kHair)/sizeof(vec3))];
        const vec3 shirt = kShirt[p.shirt  % (int)(sizeof(kShirt)/sizeof(vec3))];
        const vec3 trsrs = kTrousers[p.legs % (int)(sizeof(kTrousers)/sizeof(vec3))];
        const vec3 shoe  = kShoes[p.shoes  % (int)(sizeof(kShoes)/sizeof(vec3))];

        // torso: a touch of forward lean when walking
        addBox(out, feet, p.yaw, vec3(0, 1.02f, 0), vec3(0, 1.02f, 0), vec3(0.23f, 0.31f, 0.12f),
               0.06f * length(p.vel), shirt);
        // head + hair cap
        addBox(out, feet, p.yaw, vec3(0, 1.55f, 0), vec3(0, 1.55f, 0), vec3(0.19f, 0.19f, 0.19f),
               0.0f, skin);
        addBox(out, feet, p.yaw, vec3(0, 1.75f, 0), vec3(0, 1.75f, 0), vec3(0.20f, 0.045f, 0.20f),
               0.0f, hair);
        // arms swing opposite to the legs on the same side
        const float arm =  0.42f * s;
        const float leg = -0.52f * s;
        addBox(out, feet, p.yaw, kArmPivotL, kArmPivotL + vec3(0, -0.30f, 0), vec3(0.08f, 0.30f, 0.085f), arm, shirt);
        addBox(out, feet, p.yaw, kArmPivotR, kArmPivotR + vec3(0, -0.30f, 0), vec3(0.08f, 0.30f, 0.085f), -arm, shirt);
        // legs: trouser cloth down to the ankle, shoe block at the foot
        addBox(out, feet, p.yaw, kLegPivotL, kLegPivotL + vec3(0, -0.33f, 0), vec3(0.10f, 0.33f, 0.11f), leg, trsrs);
        addBox(out, feet, p.yaw, kLegPivotR, kLegPivotR + vec3(0, -0.33f, 0), vec3(0.10f, 0.33f, 0.11f), -leg, trsrs);
        addBox(out, feet, p.yaw, kLegPivotL, kLegPivotL + vec3(0, -0.70f, 0.02f), vec3(0.105f, 0.05f, 0.13f), leg, shoe);
        addBox(out, feet, p.yaw, kLegPivotR, kLegPivotR + vec3(0, -0.70f, 0.02f), vec3(0.105f, 0.05f, 0.13f), -leg, shoe);
    }
}

// ---------------------------------------------------------------------------
// routing graph
// ---------------------------------------------------------------------------
vec3 People::nodePos(int n) const
{
    if (n < 0 || n >= nodes) return vec3(0, TW_LEVEL, 0);
    const int i = n % TW_GRID, j = n / TW_GRID;
    return vec3(corridorCentre(i), TW_LEVEL, corridorCentre(j));
}

int People::nodeOf(float x, float z) const
{
    int bi = 0, bj = 0; float best = 1e30f;
    for (int j = 0; j < TW_GRID; ++j)
        for (int i = 0; i < TW_GRID; ++i)
        {
            const float dx = x - corridorCentre(i), dz = z - corridorCentre(j);
            const float d = dx * dx + dz * dz;
            if (d < best) { best = d; bi = i; bj = j; }
        }
    const int n = bj * TW_GRID + bi;
    if (walkableNodes > 0 && !walk[n])
    {
        // nearest walkable node, so a walker dropped in the river still finds its feet
        int alt = -1; float ad = 1e30f;
        for (int m = 0; m < nodes; ++m)
        {
            if (!walk[m]) continue;
            const vec3 p = nodePos(m);
            const float dx = x - p.x, dz = z - p.z, d = dx * dx + dz * dz;
            if (d < ad) { ad = d; alt = m; }
        }
        return alt;
    }
    return n;
}

uint32_t People::rnd()
{
    rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
    return rngState;
}

int People::irange(int lo, int hi)
{
    if (hi <= lo) return lo;
    return lo + (int)(rnd() % (uint32_t)(hi - lo + 1));
}

// --- broad phase ------------------------------------------------------------
// 4 m cells over the town: the widest thing we ask about is 1.4 m, so a walker
// only ever needs the nine cells around itself.
int People::cellIndex(float x, float z) const
{
    int i = (int)std::floor((x - gridMin) / gridCell);
    int j = (int)std::floor((z - gridMin) / gridCell);
    if (i < 0) i = 0;
    if (j < 0) j = 0;
    if (i >= gridAxis) i = gridAxis - 1;
    if (j >= gridAxis) j = gridAxis - 1;
    return j * gridAxis + i;
}

void People::buildGrid()
{
    gridCell = 4.0f;
    gridMin  = -TW_CITY_HALF - 60.0f;                  // peds never leave the lattice
    gridAxis = (int)(((TW_CITY_HALF + 60.0f) * 2.0f) / gridCell) + 1;
    cellHead.assign((size_t)gridAxis * (size_t)gridAxis, -1);
    cellNext.assign(peds.size(), -1);
    for (size_t i = 0; i < peds.size(); ++i)
    {
        const int c = cellIndex(peds[i].pos.x, peds[i].pos.z);
        cellNext[i] = cellHead[c];
        cellHead[c] = (int)i;
    }
}

void People::build(const World& w, uint32_t seed, int count)
{
    peds.clear();
    nodes = TW_GRID * TW_GRID;
    walk.assign(nodes, 0);
    nbr.assign((size_t)nodes * 4, -1);
    walkableNodes = 0; edges = 0;

    for (int j = 0; j < TW_GRID; ++j)
    for (int i = 0; i < TW_GRID; ++i)
    {
        const int n = j * TW_GRID + i;
        bool ok = true;
        if (i == TW_RIVER_V && !bridgeAlongX(j)) ok = false;   // open water, vertical river
        if (j == TW_RIVER_H && !bridgeAlongZ(i)) ok = false;   // open water, horizontal river
        if (ok) { const vec3 p = nodePos(n); if (w.blockedAt(p.x, p.z)) ok = false; }  // lamp posts, trees
        walk[n] = ok ? 1 : 0;
        walkableNodes += ok ? 1 : 0;
    }

    static const int D[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    for (int j = 0; j < TW_GRID; ++j)
    for (int i = 0; i < TW_GRID; ++i)
    {
        const int n = j * TW_GRID + i;
        if (!walk[n]) continue;
        for (int d = 0; d < 4; ++d)
        {
            const int mi = i + D[d][0], mj = j + D[d][1];
            if (mi < 0 || mj < 0 || mi >= TW_GRID || mj >= TW_GRID) continue;
            const int m = mj * TW_GRID + mi;
            if (!walk[m]) continue;
            nbr[(size_t)n * 4 + d] = m;
            if (d < 2) ++edges;                                 // count each link once
        }
    }

    rngState = seed ? seed : 0x5EED1234u;
    prev.assign(nodes, -1); seen.assign(nodes, 0);

    std::vector<int> pool; pool.reserve(walkableNodes);
    for (int m = 0; m < nodes; ++m) if (walk[m]) pool.push_back(m);
    if (pool.empty()) return;

    for (int guard = 0; (int)peds.size() < count && guard < count * 60; ++guard)
    {
        const vec3 base = nodePos(pool[irange(0, (int)pool.size() - 1)]);
        vec3 at = base; bool fits = false;
        for (int t = 0; t < 8 && !fits; ++t)
        {
            at = base + vec3(irange(-40, 40) * 0.1f, 0.0f, irange(-40, 40) * 0.1f);
            fits = !w.blockedAt(at.x, at.z);
        }
        if (!fits) continue;
        Person p;
        p.pos    = vec3(at.x, TW_LEVEL, at.z);
        p.anchor = p.pos;
        p.yaw    = irange(0, 628) * 0.01f;
        p.speed  = 1.05f + 0.65f * (irange(0, 100) / 100.0f);
        p.phase  = 6.2831853f * (irange(0, 100) / 100.0f);
        p.skin   = irange(0, 4); p.hair = irange(0, 3); p.shirt = irange(0, 7);
        p.legs   = irange(0, 4); p.shoes = irange(0, 2);
        p.sideV  = p.pos.x > kRiverVX ? 1.0f : -1.0f;
        p.sideH  = p.pos.z > kRiverHZ ? 1.0f : -1.0f;
        peds.push_back(p);
    }
}

bool People::plan(int from, int to, std::vector<int>& outPath)
{
    outPath.clear();
    if (from < 0 || to < 0 || from >= nodes || to >= nodes) return false;
    if (!walk[from] || !walk[to]) return false;
    std::fill(seen.begin(), seen.end(), (unsigned char)0);
    std::fill(prev.begin(), prev.end(), -1);
    std::vector<int> q; q.reserve(nodes); size_t head = 0;
    seen[from] = 1; q.push_back(from);
    while (head < q.size())
    {
        const int n = q[head++];
        if (n == to) break;
        for (int d = 0; d < 4; ++d)
        {
            const int m = nbr[(size_t)n * 4 + d];
            if (m >= 0 && !seen[m]) { seen[m] = 1; prev[m] = n; q.push_back(m); }
        }
    }
    if (!seen[to]) return false;
    for (int n = to; n != -1; n = prev[n]) outPath.push_back(n);
    std::reverse(outPath.begin(), outPath.end());
    if (outPath.size() > 1) outPath.erase(outPath.begin());   // the one they stand on
    return !outPath.empty();
}

int People::hops(int from, int to)
{
    std::vector<int> p;
    if (!plan(from, to, p)) return -1;
    return (int)p.size();
}

// ---------------------------------------------------------------------------
// simulation
// ---------------------------------------------------------------------------
// Note: obstacle tests use World::blockedAt, which measures with TW_PLAYER_R
// (0.45 m). A walker is 0.30 m across the shoulders, so this asks for extra
// clearance around walls, railings and water - never less than they need.
void People::update(const World& w, float dt, const vec3& playerPos)
{
    if (dt <= 0.0f || peds.empty()) return;
    if (dt > 0.1f) dt = 0.1f;                       // a tab-out must not teleport anyone
    buildGrid();

    for (size_t idx = 0; idx < peds.size(); ++idx)
    {
        Person& p = peds[idx];
        if (p.wait > 0.0f) { p.wait -= dt; p.vel = vec3(0, 0, 0); continue; }

        // ---- route: somewhere a good distance away, or nothing to do --------
        if (p.at >= p.path.size())
        {
            const int from = nodeOf(p.pos.x, p.pos.z);

            // Most errands are downtown-bound - that is where the traffic is -
            // but a good slice of chance keeps side streets and suburbs busy too.
            int to = -1; float toScore = 1e30f;
            for (int t = 0; t < 10; ++t)
            {
                const int c = (int)(rnd() % (uint32_t)(nodes > 0 ? nodes : 1));
                if (c < 0 || !walk[c] || c == from) continue;
                const vec3 cp = nodePos(c);
                const float score = std::sqrt(cp.x * cp.x + cp.z * cp.z) + irange(0, 420) * 1.0f;
                if (score < toScore) { toScore = score; to = c; }
            }
            if (to >= 0 && hops(from, to) < 4) to = -1;      // not worth getting up for
            if (to < 0)                                      // last resort: anything reachable
                for (int t = 0; t < 24 && to < 0; ++t)
                {
                    const int c = (int)(rnd() % (uint32_t)(nodes > 0 ? nodes : 1));
                    if (c < 0 || !walk[c] || c == from) continue;
                    if (hops(from, c) >= 4) to = c;
                }
            if (to < 0) { p.wait = 0.9f; continue; }
            p.dest = to;
            if (!plan(from, to, p.path))
            {
                p.path.clear(); p.at = 0; p.dest = -1; p.wait = 1.5f; continue;
            }
            p.at = 0;
        }

        // ---- steer at the next intersection on the route --------------------
        const vec3 wp = nodePos(p.path[p.at]);
        const vec3 d(wp.x - p.pos.x, 0.0f, wp.z - p.pos.z);
        const float dl = length(d);
        if (dl < 1.0f)
        {
            ++p.at;
            if (p.at >= p.path.size())
            {
                p.dest = -1; p.vel = vec3(0, 0, 0);
                p.wait = 0.4f + 2.2f * (irange(0, 100) / 100.0f);      // stand about a while
                continue;
            }
        }
        vec3 want = (dl > 1e-4f) ? d * (p.speed / dl) : vec3(0, 0, 0);

        // ---- avoidance: the player has right of way, then each other --------
        vec3 sep(0, 0, 0);
        {
            const vec3 s(p.pos.x - playerPos.x, 0.0f, p.pos.z - playerPos.z);
            const float dd = length(s), nr = p.radius + 1.05f;
            if (dd > 1e-4f && dd < nr) sep = sep + s * ((1.0f / dd) * 3.2f * (1.0f - dd / nr));
        }
        const int myCell = cellIndex(p.pos.x, p.pos.z);
        const int myX = myCell % gridAxis, myZ = myCell / gridAxis;
        for (int ci = -1; ci <= 1; ++ci)
        for (int cj = -1; cj <= 1; ++cj)
        {
            const int bx = myX + ci, bz = myZ + cj;
            if (bx < 0 || bz < 0 || bx >= gridAxis || bz >= gridAxis) continue;
            for (int k = cellHead[bz * gridAxis + bx]; k >= 0; k = cellNext[k])
            {
                if ((size_t)k == idx) continue;
                const Person& o = peds[k];
                const vec3 s(p.pos.x - o.pos.x, 0.0f, p.pos.z - o.pos.z);
                const float dd = length(s), nr = p.radius + o.radius + 0.5f;
                if (dd > 1e-4f && dd < nr) sep = sep + s * ((1.0f / dd) * 2.0f * (1.0f - dd / nr));
            }
        }
        vec3 v = want + sep;
        const float vl = length(v);
        if (vl > 1e-4f) v = v * (p.speed / vl);
        p.vel = v;

        // ---- move, sliding along walls, in steps too short to tunnel --------
        const float travel = length(v) * dt;
        const int steps = travel > 0.2f ? (int)ceilf(travel / 0.2f) : 1;
        const vec3 before = p.pos;
        for (int s = 0; s < steps; ++s)
        {
            const float ox = v.x * dt / (float)steps, oz = v.z * dt / (float)steps;
            if (!w.blockedAt(p.pos.x + ox, p.pos.z)) p.pos.x += ox;
            if (!w.blockedAt(p.pos.x, p.pos.z + oz)) p.pos.z += oz;
        }
        const float moved = length(vec3(p.pos.x - before.x, 0.0f, p.pos.z - before.z));
        p.phase += moved * 4.6f;                                   // stride length
        if (moved > 1e-4f && length(v) > 0.05f) p.yaw = std::atan2(v.x, -v.z);

        // ---- river crossings: the water is unwalkable, so each one was on a bridge
        const float sv = p.pos.x > kRiverVX ? 1.0f : -1.0f;
        if (p.sideV == 0.0f) p.sideV = sv;
        else if (sv != p.sideV) { p.sideV = sv; ++p.crossings; }
        const float sh = p.pos.z > kRiverHZ ? 1.0f : -1.0f;
        if (p.sideH == 0.0f) p.sideH = sh;
        else if (sh != p.sideH) { p.sideH = sh; ++p.crossings; }

        // ---- watchdog: something has them cornered, so find a new route -----
        if (length(vec3(p.pos.x - p.anchor.x, 0.0f, p.pos.z - p.anchor.z)) > 1.8f)
        {
            p.anchor = p.pos; p.idleT = 0.0f;
        }
        else
        {
            p.idleT += dt;
            if (p.idleT > 2.0f)
            {
                p.idleT = 0.0f; p.path.clear(); p.at = 0; p.dest = -1;
                p.anchor = p.pos;
            }
        }
    }

    // ---- hard guarantee: no two bodies ever share space ---------------------
    // Steering only nudges. This resolves what is left over, so two walkers
    // meeting head-on in the middle of a bridge end up side by side instead of
    // merged, and nobody can be squeezed through the player either.
    auto tryStep = [&](Person& q, float ox, float oz) -> bool
    {
        const float nx = q.pos.x + ox, nz = q.pos.z + oz;
        if (!w.blockedAt(nx, nz))            { q.pos.x = nx; q.pos.z = nz; return true; }
        if (!w.blockedAt(nx, q.pos.z))       { q.pos.x = nx;               return true; }
        if (!w.blockedAt(q.pos.x, nz))       { q.pos.z = nz;               return true; }
        return false;   // boxed in: a hair of overlap beats being inside a wall
    };

    for (int pass = 0; pass < 3; ++pass)
    {
        for (size_t a = 0; a < peds.size(); ++a)
        {
            const int ca = cellIndex(peds[a].pos.x, peds[a].pos.z);
            const int ax = ca % gridAxis, az = ca / gridAxis;
            for (int ci = -1; ci <= 1; ++ci)
            for (int cj = -1; cj <= 1; ++cj)
            {
                const int bx = ax + ci, bz = az + cj;
                if (bx < 0 || bz < 0 || bx >= gridAxis || bz >= gridAxis) continue;
                for (int b = cellHead[bz * gridAxis + bx]; b >= 0; b = cellNext[b])
                {
                    if ((size_t)b <= a) continue;                 // each pair once
                    Person& A = peds[a]; Person& B = peds[b];
                    float dx = B.pos.x - A.pos.x, dz = B.pos.z - A.pos.z;
                    float dd = std::sqrt(dx * dx + dz * dz);
                    const float need = A.radius + B.radius;
                    if (dd >= need) continue;
                    if (dd < 1e-4f) { dx = 1.0f; dz = 0.0f; dd = 1.0f; }   // dead centre: push along x
                    const float ux = dx / dd, uz = dz / dd, push = (need - dd) * 0.5f;
                    tryStep(A, -ux * push, -uz * push);
                    tryStep(B,  ux * push,  uz * push);
                }
            }
        }
        // the player is immovable, so walkers get pushed off them instead
        for (size_t a = 0; a < peds.size(); ++a)
        {
            Person& A = peds[a];
            float dx = A.pos.x - playerPos.x, dz = A.pos.z - playerPos.z;
            float dd = std::sqrt(dx * dx + dz * dz);
            const float need = A.radius + TW_PLAYER_R;
            if (dd >= need) continue;
            if (dd < 1e-4f) { dx = 1.0f; dz = 0.0f; dd = 1.0f; }
            tryStep(A, dx / dd * (need - dd), dz / dd * (need - dd));
        }
    }
}

// ---------------------------------------------------------------------------
// the player collides with them
// ---------------------------------------------------------------------------
bool People::blocks(const vec3& p, float r) const
{
    for (size_t i = 0; i < peds.size(); ++i)
    {
        const Person& q = peds[i];
        const float dx = p.x - q.pos.x, dz = p.z - q.pos.z;
        const float need = r + q.radius;
        if (dx * dx + dz * dz < need * need) return true;
    }
    return false;
}

void People::pushOut(vec3& p, float r) const
{
    for (int pass = 0; pass < 4; ++pass)
        for (size_t i = 0; i < peds.size(); ++i)
        {
            const Person& q = peds[i];
            float dx = p.x - q.pos.x, dz = p.z - q.pos.z;
            float dd = std::sqrt(dx * dx + dz * dz);
            const float need = r + q.radius;
            if (dd >= need) continue;
            if (dd < 1e-4f) { dx = 1.0f; dz = 0.0f; dd = 1.0f; }
            p.x += dx / dd * (need - dd);
            p.z += dz / dd * (need - dd);
        }
}

// ---------------------------------------------------------------------------
// diagnostics (the selftest proves the simulation from these numbers)
// ---------------------------------------------------------------------------
People::Stats People::stats(const World& w) const
{
    Stats s;
    s.total = (int)peds.size();
    for (size_t i = 0; i < peds.size(); ++i)
    {
        const Person& p = peds[i];
        (length(p.vel) > 0.05f ? s.walking : s.waiting)++;
        s.crossings += p.crossings;
        if (w.blockedAt(p.pos.x, p.pos.z)) ++s.inSolid;

        bool inW = false, bridge = false;
        for (size_t k = 0; k < w.waterRects.size(); ++k)
        {
            const AABB& r = w.waterRects[k];
            if (p.pos.x > r.minX && p.pos.x < r.maxX && p.pos.z > r.minZ && p.pos.z < r.maxZ) inW = true;
        }
        for (size_t k = 0; k < w.bridgeZones.size(); ++k)
        {
            const AABB& r = w.bridgeZones[k];
            if (p.pos.x > r.minX && p.pos.x < r.maxX && p.pos.z > r.minZ && p.pos.z < r.maxZ) bridge = true;
        }
        if (inW && !bridge) ++s.inWater;
        if (inW &&  bridge) ++s.onBridge;

        for (size_t k = i + 1; k < peds.size(); ++k)
        {
            const float dx = p.pos.x - peds[k].pos.x, dz = p.pos.z - peds[k].pos.z;
            const float need = (p.radius + peds[k].radius) * 0.98f;
            if (dx * dx + dz * dz < need * need) ++s.overlaps;
        }
    }
    return s;
}

void People::simulate(const World& w, float seconds, const vec3& playerStill)
{
    const float h = 1.0f / 60.0f;
    const int n = (int)(seconds / h);
    for (int i = 0; i < n; ++i) update(w, h, playerStill);
}

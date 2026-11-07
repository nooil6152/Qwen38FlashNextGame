// world.cpp - procedural town generator.
//
// Layout: a GRID x GRID of city blocks. Every block is preceded by a street
// corridor, so streets form a clean lattice. Two of those corridors are widened
// into rivers, which means (a) no block ever overlaps water and (b) bridges can
// only exist where a street meets a river - exactly what the brief asks for.
//
// Everything emitted here is a flat-shaded box / quad / pyramid and every solid
// footprint goes into World::blockers, so walking into a building is impossible
// by construction.
#include "world.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// small deterministic RNG (xorshift32): same seed -> same town
// ---------------------------------------------------------------------------
namespace
{
struct Rng
{
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float f() { return (float)(next() >> 8) * (1.0f / 16777216.0f); }   // [0,1)
    float range(float a, float b) { return a + (b - a) * f(); }
    int   irange(int a, int b) { return a + (int)(f() * (float)(b - a + 1)); }
    bool  chance(float p) { return f() < p; }
};

inline uint32_t hash2(int a, int b, uint32_t seed)
{
    uint32_t h = seed ^ ((uint32_t)a * 0x01000193u) ^ ((uint32_t)b * 0x85EBCA6Bu);
    h ^= h >> 15; h *= 0x2545F491u; h ^= h >> 13;
    return h;
}

// ---------------------------------------------------------------------------
// rects
// ---------------------------------------------------------------------------
struct Rect { float x0, x1, z0, z1; };

inline bool intersects(const Rect& a, const Rect& b)
{
    return a.x0 < b.x1 && b.x0 < a.x1 && a.z0 < b.z1 && b.z0 < a.z1;
}

inline bool inside(const Rect& outer, const Rect& inner)
{
    return inner.x0 >= outer.x0 && inner.x1 <= outer.x1 &&
           inner.z0 >= outer.z0 && inner.z1 <= outer.z1;
}

// subtract one rect from another -> up to 4 remaining rects
static void subtractRect(const Rect& r, const Rect& c, std::vector<Rect>& out)
{
    if (!intersects(r, c)) { out.push_back(r); return; }
    if (r.x0 < c.x0) out.push_back(Rect{ r.x0, c.x0, r.z0, r.z1 });
    if (c.x1 < r.x1) out.push_back(Rect{ c.x1, r.x1, r.z0, r.z1 });
    const float ix0 = std::max(r.x0, c.x0), ix1 = std::min(r.x1, c.x1);
    if (r.z0 < c.z0) out.push_back(Rect{ ix0, ix1, r.z0, c.z0 });
    if (c.z1 < r.z1) out.push_back(Rect{ ix0, ix1, c.z1, r.z1 });
}

static std::vector<Rect> cutOut(const Rect& r, const std::vector<Rect>& cuts)
{
    std::vector<Rect> cur, nxt;
    cur.push_back(r);
    for (size_t k = 0; k < cuts.size(); ++k)
    {
        nxt.clear();
        for (size_t q = 0; q < cur.size(); ++q) subtractRect(cur[q], cuts[k], nxt);
        cur.swap(nxt);
    }
    return cur;
}

// ---------------------------------------------------------------------------
// geometry emitters
// ---------------------------------------------------------------------------
inline Vertex makeV(float x, float y, float z, float nx, float ny, float nz,
                    const vec3& c, float win, float glow)
{
    Vertex v;
    v.x = x; v.y = y; v.z = z;
    v.nx = nx; v.ny = ny; v.nz = nz;
    v.r = c.x; v.g = c.y; v.b = c.z;
    v.win = win; v.glow = glow;
    return v;
}

// two triangles, CCW as seen from the outside (normal side)
static void quad(std::vector<Vertex>& v, const vec3& p0, const vec3& p1,
                 const vec3& p2, const vec3& p3, const vec3& n,
                 const vec3& c, float win = 0.0f, float glow = 0.0f)
{
    const vec3 nn = normalize(n);
    v.push_back(makeV(p0.x, p0.y, p0.z, nn.x, nn.y, nn.z, c, win, glow));
    v.push_back(makeV(p1.x, p1.y, p1.z, nn.x, nn.y, nn.z, c, win, glow));
    v.push_back(makeV(p2.x, p2.y, p2.z, nn.x, nn.y, nn.z, c, win, glow));
    v.push_back(makeV(p0.x, p0.y, p0.z, nn.x, nn.y, nn.z, c, win, glow));
    v.push_back(makeV(p2.x, p2.y, p2.z, nn.x, nn.y, nn.z, c, win, glow));
    v.push_back(makeV(p3.x, p3.y, p3.z, nn.x, nn.y, nn.z, c, win, glow));
}

static void flatQuad(std::vector<Vertex>& v, const Rect& r, float y, const vec3& c)
{
    quad(v, vec3(r.x0, y, r.z1), vec3(r.x1, y, r.z1), vec3(r.x1, y, r.z0), vec3(r.x0, y, r.z0),
         vec3(0, 1, 0), c);
}

// axis aligned box; the top face is tinted towards grey so flat roofs read as
// gravel/membrane instead of "another wall"
static void box(std::vector<Vertex>& v, float x0, float x1, float y0, float y1,
                float z0, float z1, const vec3& col, float win, float glow,
                float roofMix = 0.55f)
{
    const vec3 roof = lerp(col, vec3(0.30f, 0.30f, 0.30f), roofMix);
    quad(v, vec3(x1, y0, z1), vec3(x1, y0, z0), vec3(x1, y1, z0), vec3(x1, y1, z1),
         vec3(1, 0, 0), col, win, glow);
    quad(v, vec3(x0, y0, z0), vec3(x0, y0, z1), vec3(x0, y1, z1), vec3(x0, y1, z0),
         vec3(-1, 0, 0), col, win, glow);
    quad(v, vec3(x0, y0, z1), vec3(x1, y0, z1), vec3(x1, y1, z1), vec3(x0, y1, z1),
         vec3(0, 0, 1), col, win, glow);
    quad(v, vec3(x1, y0, z0), vec3(x0, y0, z0), vec3(x0, y1, z0), vec3(x1, y1, z0),
         vec3(0, 0, -1), col, win, glow);
    quad(v, vec3(x0, y1, z1), vec3(x1, y1, z1), vec3(x1, y1, z0), vec3(x0, y1, z0),
         vec3(0, 1, 0), roof, 0.0f, glow);
    quad(v, vec3(x0, y0, z0), vec3(x1, y0, z0), vec3(x1, y0, z1), vec3(x0, y0, z1),
         vec3(0, -1, 0), roof, 0.0f, glow);
}

// hip roof: apex over the centre of a base rect
static void pyramid(std::vector<Vertex>& v, float x0, float x1, float y, float z0, float z1,
                    float h, const vec3& col)
{
    const float cx = (x0 + x1) * 0.5f, cz = (z0 + z1) * 0.5f;
    const vec3 a = vec3(cx, y + h, cz);
    quad(v, vec3(x0, y, z1), vec3(x1, y, z1), a, a, vec3(0, 0.6f, 0.8f), col);
    quad(v, vec3(x1, y, z1), vec3(x1, y, z0), a, a, vec3(0.8f, 0.6f, 0.0f), col);
    quad(v, vec3(x1, y, z0), vec3(x0, y, z0), a, a, vec3(0, 0.6f, -0.8f), col);
    quad(v, vec3(x0, y, z0), vec3(x0, y, z1), a, a, vec3(-0.8f, 0.6f, 0.0f), col);
}

// Wall run with gaps punched in it (bridges and the crossing river).
// alongX: wall extends along x and has constant z (fixedLo..fixedHi), else the
// other way round.
static void wallWithGaps(std::vector<Vertex>& v, bool alongX, float fixedLo, float fixedHi,
                         float from, float to, float y0, float y1,
                         std::vector<std::pair<float, float> > gaps, const vec3& col)
{
    std::sort(gaps.begin(), gaps.end());
    float cursor = from;
    for (size_t k = 0; k < gaps.size(); ++k)
    {
        const float g0 = std::max(from, gaps[k].first);
        const float g1 = std::min(to, gaps[k].second);
        if (g1 <= g0) continue;
        if (g0 > cursor)
        {
            if (alongX) box(v, cursor, g0, y0, y1, fixedLo, fixedHi, col, 0.0f, 0.0f, 0.35f);
            else        box(v, fixedLo, fixedHi, y0, y1, cursor, g0, col, 0.0f, 0.0f, 0.35f);
        }
        cursor = std::max(cursor, g1);
    }
    if (cursor < to)
    {
        if (alongX) box(v, cursor, to, y0, y1, fixedLo, fixedHi, col, 0.0f, 0.0f, 0.35f);
        else        box(v, fixedLo, fixedHi, y0, y1, cursor, to, col, 0.0f, 0.0f, 0.35f);
    }
}

// ---------------------------------------------------------------------------
// palette
// ---------------------------------------------------------------------------
const vec3 C_ASPHALT(0.135f, 0.140f, 0.155f);
const vec3 C_PAVE   (0.360f, 0.360f, 0.375f);
const vec3 C_PLAZA  (0.470f, 0.460f, 0.440f);
const vec3 C_GRASS  (0.245f, 0.335f, 0.185f);
const vec3 C_FIELD  (0.400f, 0.420f, 0.255f);
const vec3 C_STONE  (0.430f, 0.420f, 0.395f);
const vec3 C_QUAY   (0.520f, 0.510f, 0.480f);
const vec3 C_BED    (0.110f, 0.130f, 0.150f);
const vec3 C_DECK   (0.290f, 0.295f, 0.315f);
const vec3 C_RAIL   (0.640f, 0.640f, 0.615f);
const vec3 C_METAL  (0.200f, 0.205f, 0.215f);
const vec3 C_LAMP   (0.930f, 0.880f, 0.660f);
const vec3 C_TRUNK  (0.270f, 0.190f, 0.135f);
const vec3 C_WATER  (0.090f, 0.250f, 0.350f);

const vec3 GLASS[] = {
    vec3(0.270f, 0.350f, 0.450f), vec3(0.190f, 0.240f, 0.320f), vec3(0.380f, 0.420f, 0.470f),
    vec3(0.300f, 0.320f, 0.380f), vec3(0.230f, 0.290f, 0.390f), vec3(0.450f, 0.470f, 0.500f),
    vec3(0.340f, 0.300f, 0.300f)
};
const vec3 MIDC[] = {
    vec3(0.550f, 0.500f, 0.450f), vec3(0.430f, 0.420f, 0.420f), vec3(0.620f, 0.570f, 0.500f),
    vec3(0.340f, 0.400f, 0.450f), vec3(0.500f, 0.460f, 0.400f), vec3(0.400f, 0.440f, 0.480f),
    vec3(0.580f, 0.520f, 0.430f)
};
const vec3 HOUSEC[] = {
    vec3(0.720f, 0.630f, 0.500f), vec3(0.600f, 0.440f, 0.380f), vec3(0.520f, 0.560f, 0.620f),
    vec3(0.760f, 0.720f, 0.640f), vec3(0.660f, 0.520f, 0.440f), vec3(0.470f, 0.520f, 0.450f)
};
const vec3 ROOFC[] = {
    vec3(0.420f, 0.215f, 0.170f), vec3(0.290f, 0.275f, 0.290f),
    vec3(0.470f, 0.300f, 0.215f), vec3(0.250f, 0.250f, 0.270f)
};
const vec3 LEAF[] = {
    vec3(0.150f, 0.300f, 0.140f), vec3(0.200f, 0.340f, 0.150f), vec3(0.130f, 0.260f, 0.130f)
};

template <size_t N>
inline const vec3& pick(const vec3(&arr)[N], Rng& r) { return arr[r.irange(0, (int)N - 1)]; }

inline void addBlocker(std::vector<AABB>& out, float x0, float x1, float z0, float z1)
{
    AABB b; b.minX = x0; b.maxX = x1; b.minZ = z0; b.maxZ = z1;
    out.push_back(b);
}

struct Cor
{
    float operator()(int i) const { return -TW_CITY_HALF + (float)i * TW_CELL; }
};
} // namespace

// ---------------------------------------------------------------------------
// World::build
// ---------------------------------------------------------------------------
void World::build(uint32_t seed)
{
    solid.clear(); water.clear(); clouds.clear();
    blockers.clear(); waterRects.clear(); bridgeZones.clear();

    Cor cor;
    const float vx = cor(TW_RIVER_V) + TW_ROAD * 0.5f;   // vertical river centre x
    const float hz = cor(TW_RIVER_H) + TW_ROAD * 0.5f;   // horizontal river centre z

    Rect riverV{ vx - TW_RIVER_VH, vx + TW_RIVER_VH, -TW_RIVER_LEN, TW_RIVER_LEN };
    Rect riverH{ -TW_RIVER_LEN, TW_RIVER_LEN, hz - TW_RIVER_HH, hz + TW_RIVER_HH };
    std::vector<Rect> waterCuts;
    waterCuts.push_back(riverV);
    waterCuts.push_back(riverH);

    AABB wv = { riverV.x0, riverV.z0, riverV.x1, riverV.z1 };
    AABB wh = { riverH.x0, riverH.z0, riverH.x1, riverH.z1 };
    waterRects.push_back(wv);
    waterRects.push_back(wh);

    // ---- bridges: only where a street meets a river -----------------------
    struct Bridge { Rect deck; bool alongZ; };  // alongZ: crosses the horizontal river
    std::vector<Bridge> bridges;
    for (int i = 0; i < TW_GRID; ++i)
    {
        if (i == TW_RIVER_V || (i % 2) != 0) continue;   // corridor #8 is the other river
        Bridge b; b.alongZ = true;
        b.deck = Rect{ cor(i) - 0.6f, cor(i) + TW_ROAD + 0.6f,
                       hz - TW_RIVER_HH - 3.0f, hz + TW_RIVER_HH + 3.0f };
        bridges.push_back(b);
    }
    for (int j = 0; j < TW_GRID; ++j)
    {
        if (j == TW_RIVER_H || (j % 2) != 0) continue;
        Bridge b; b.alongZ = false;
        b.deck = Rect{ vx - TW_RIVER_VH - 3.0f, vx + TW_RIVER_VH + 3.0f,
                       cor(j) - 0.6f, cor(j) + TW_ROAD + 0.6f };
        bridges.push_back(b);
    }

    // ---- terrain: one big rect with the rivers cut out --------------------
    // Leaves exactly the 4 corner pieces, so nothing is ever drawn underneath
    // the water and the rivers appear to carry on past the fog line.
    {
        Rect big{ -TW_TERRAIN, TW_TERRAIN, -TW_TERRAIN, TW_TERRAIN };
        std::vector<Rect> pieces = cutOut(big, waterCuts);
        for (size_t k = 0; k < pieces.size(); ++k) flatQuad(solid, pieces[k], TW_FIELD_Y, C_FIELD);
    }

    // ---- rivers: riverbed, banks, quay copings ---------------------------
    {
        std::vector<Rect> hp(1, riverH);
        std::vector<Rect> vPieces = cutOut(riverV, hp);   // avoid coplanar water in the pool
        for (size_t k = 0; k < vPieces.size(); ++k)
        {
            flatQuad(water, vPieces[k], TW_WATER_Y, C_WATER);
            flatQuad(solid, vPieces[k], TW_RIVERBED_Y, C_BED);
        }
        flatQuad(water, riverH, TW_WATER_Y, C_WATER);
        flatQuad(solid, riverH, TW_RIVERBED_Y, C_BED);
    }

    std::vector<std::pair<float, float> > gapsV, gapsH;
    gapsV.push_back(std::make_pair(riverV.x0 - 0.8f, riverV.x1 + 0.8f));
    gapsH.push_back(std::make_pair(riverH.z0 - 0.8f, riverH.z1 + 0.8f));
    for (size_t k = 0; k < bridges.size(); ++k)
    {
        if (bridges[k].alongZ) gapsV.push_back(std::make_pair(bridges[k].deck.x0, bridges[k].deck.x1));
        else                   gapsH.push_back(std::make_pair(bridges[k].deck.z0, bridges[k].deck.z1));
    }

    const float wb = TW_RIVERBED_Y;
    for (int side = 0; side < 2; ++side)
    {
        // horizontal river: banks are walls of constant z, extending along x
        const float zc = side == 0 ? hz - TW_RIVER_HH : hz + TW_RIVER_HH;
        const float lo = side == 0 ? zc - 0.7f : zc;
        const float hi = side == 0 ? zc : zc + 0.7f;
        const float clo = side == 0 ? zc - TW_QUAY_W : zc;
        const float chi = side == 0 ? zc : zc + TW_QUAY_W;
        wallWithGaps(solid, true, lo, hi, -TW_RIVER_LEN, TW_RIVER_LEN, wb, 0.0f, gapsV, C_STONE);
        wallWithGaps(solid, true, clo, chi, -TW_RIVER_LEN, TW_RIVER_LEN, 0.0f, TW_QUAY_Y, gapsV, C_QUAY);
        // vertical river: walls of constant x, extending along z
        const float xc = side == 0 ? vx - TW_RIVER_VH : vx + TW_RIVER_VH;
        const float xlo = side == 0 ? xc - 0.7f : xc;
        const float xhi = side == 0 ? xc : xc + 0.7f;
        const float xclo = side == 0 ? xc - TW_QUAY_W : xc;
        const float xchi = side == 0 ? xc : xc + TW_QUAY_W;
        wallWithGaps(solid, false, xlo, xhi, -TW_RIVER_LEN, TW_RIVER_LEN, wb, 0.0f, gapsH, C_STONE);
        wallWithGaps(solid, false, xclo, xchi, -TW_RIVER_LEN, TW_RIVER_LEN, 0.0f, TW_QUAY_Y, gapsH, C_QUAY);
    }

    // ---- bridge decks, railings, walkable zones --------------------------
    for (size_t k = 0; k < bridges.size(); ++k)
    {
        const Bridge& b = bridges[k];
        // The slab goes all the way down to the riverbed, which closes the gap
        // that the bank wall had to leave for the road.
        box(solid, b.deck.x0, b.deck.x1, wb, TW_DECK_Y, b.deck.z0, b.deck.z1, C_DECK, 0.0f, 0.0f, 0.25f);
        const float rt = TW_RAIL_T;
        const float rx0 = b.deck.x0 + rt, rx1 = b.deck.x1 - rt;
        const float rz0 = b.deck.z0 + rt, rz1 = b.deck.z1 - rt;
        if (b.alongZ)
        {
            box(solid, b.deck.x0, rx0, TW_DECK_Y, 1.18f, b.deck.z0, b.deck.z1, C_RAIL, 0.0f, 0.0f, 0.1f);
            box(solid, rx1, b.deck.x1, TW_DECK_Y, 1.18f, b.deck.z0, b.deck.z1, C_RAIL, 0.0f, 0.0f, 0.1f);
            addBlocker(blockers, b.deck.x0, rx0, b.deck.z0, b.deck.z1);
            addBlocker(blockers, rx1, b.deck.x1, b.deck.z0, b.deck.z1);
        }
        else
        {
            box(solid, b.deck.x0, b.deck.x1, TW_DECK_Y, 1.18f, b.deck.z0, rz0, C_RAIL, 0.0f, 0.0f, 0.1f);
            box(solid, b.deck.x0, b.deck.x1, TW_DECK_Y, 1.18f, rz1, b.deck.z1, C_RAIL, 0.0f, 0.0f, 0.1f);
            addBlocker(blockers, b.deck.x0, b.deck.x1, b.deck.z0, rz0);
            addBlocker(blockers, b.deck.x0, b.deck.x1, rz1, b.deck.z1);
        }
        // Walkable strip = the exemption from the "water is not walkable" rule.
        // Deliberately NOT inflated: the railings are what stop the player, and
        // the exemption must not extend past them or you could sidestep into
        // the river beside the deck.
        AABB zone = { rx0, rz0, rx1, rz1 };
        bridgeZones.push_back(zone);
    }

    // ---- per-block content ------------------------------------------------
    std::vector<Rect> footprints;   // reset for every block

    auto canPlace = [&](const Rect& lot, const Rect& fp) -> bool
    {
        if (!inside(lot, fp)) return false;
        Rect gv = { riverV.x0 - 1.5f, riverV.x1 + 1.5f, riverV.z0, riverV.z1 };
        Rect gh = { riverH.x0, riverH.x1, riverH.z0 - 1.5f, riverH.z1 + 1.5f };
        if (intersects(fp, gv) || intersects(fp, gh)) return false;
        for (size_t k = 0; k < footprints.size(); ++k)
            if (intersects(fp, footprints[k])) return false;
        return true;
    };

    int buildingCount = 0, treeCount = 0;

    auto tree = [&](float tx, float tz, float s, Rng& r)
    {
        const float th = 2.4f * s;
        box(solid, tx - 0.22f * s, tx + 0.22f * s, 0.0f, th,
            tz - 0.22f * s, tz + 0.22f * s, C_TRUNK, 0.0f, 0.0f, 0.2f);
        const float cr = 1.9f * s;
        pyramid(solid, tx - cr, tx + cr, th - 0.3f * s, tz - cr, tz + cr, 4.2f * s, pick(LEAF, r));
        addBlocker(blockers, tx - 0.35f * s, tx + 0.35f * s, tz - 0.35f * s, tz + 0.35f * s);
        ++treeCount;
    };

    auto tryTree = [&](const Rect& lot, Rng& r, float smin, float smax)
    {
        for (int a = 0; a < 6; ++a)
        {
            const float s = r.range(smin, smax);
            const float tx = r.range(lot.x0 + 2.6f, lot.x1 - 2.6f);
            const float tz = r.range(lot.z0 + 2.6f, lot.z1 - 2.6f);
            Rect fp{ tx - 2.0f * s, tx + 2.0f * s, tz - 2.0f * s, tz + 2.0f * s };
            if (!canPlace(lot, fp)) continue;
            footprints.push_back(fp);
            tree(tx, tz, s, r);
            return;
        }
    };

    auto building = [&](float cx, float cz, float w, float d, float h, const vec3& col,
                        float win, bool hipRoof, Rng& r)
    {
        Rect fp{ cx - w * 0.5f, cx + w * 0.5f, cz - d * 0.5f, cz + d * 0.5f };
        footprints.push_back(fp);
        box(solid, fp.x0, fp.x1, 0.0f, h, fp.z0, fp.z1, col, win, r.f());
        addBlocker(blockers, fp.x0, fp.x1, fp.z0, fp.z1);
        if (hipRoof)
            pyramid(solid, fp.x0 - 0.35f, fp.x1 + 0.35f, h, fp.z0 - 0.35f, fp.z1 + 0.35f,
                    r.range(2.0f, 3.4f), pick(ROOFC, r));
        ++buildingCount;
    };

    for (int i = 0; i < TW_GRID; ++i)
    {
        for (int j = 0; j < TW_GRID; ++j)
        {
            Rect lot{ cor(i) + TW_ROAD, cor(i) + TW_CELL, cor(j) + TW_ROAD, cor(j) + TW_CELL };
            const float ccx = (lot.x0 + lot.x1) * 0.5f;
            const float ccz = (lot.z0 + lot.z1) * 0.5f;
            const float dist = std::sqrt(ccx * ccx + ccz * ccz);
            Rng rng(hash2(i, j, seed));

            // 0 = financial core, 1 = mid rows, 2 = suburbs, 3 = fringe
            const int district = dist < 120.0f ? 0 : dist < 245.0f ? 1 : dist < 330.0f ? 2 : 3;

            // lot ground, clipped so a lot next to a river stops at the bank
            std::vector<Rect> lotPieces = cutOut(lot, waterCuts);
            const vec3 lotCol = district == 0 ? C_PLAZA : district == 1 ? C_PAVE
                              : district == 2 ? C_GRASS : vec3(0.31f, 0.36f, 0.22f);
            for (size_t k = 0; k < lotPieces.size(); ++k)
                if (lotPieces[k].x1 - lotPieces[k].x0 > 0.4f && lotPieces[k].z1 - lotPieces[k].z0 > 0.4f)
                    flatQuad(solid, lotPieces[k], TW_LOT_Y, lotCol);

            footprints.clear();

            if (district == 0)
            {
                if (rng.chance(0.10f))                    // pocket plaza
                {
                    const int n = rng.irange(3, 6);
                    for (int t = 0; t < n; ++t) tryTree(lot, rng, 1.0f, 1.6f);
                }
                else
                {
                    const float shrink = 1.0f - clampf(dist / 120.0f, 0.0f, 1.0f) * 0.35f;
                    const float w = rng.range(24.0f, 33.0f) * shrink;
                    const float d = rng.range(24.0f, 33.0f) * shrink;
                    const float h = rng.range(80.0f, 155.0f) * (0.55f + 0.45f * shrink);
                    const float bx = ccx + rng.range(-1.0f, 1.0f) * (TW_BLOCK - w) * 0.30f;
                    const float bz = ccz + rng.range(-1.0f, 1.0f) * (TW_BLOCK - d) * 0.30f;
                    Rect fp{ bx - w * 0.5f, bx + w * 0.5f, bz - d * 0.5f, bz + d * 0.5f };
                    if (canPlace(lot, fp))
                    {
                        const vec3 col = pick(GLASS, rng);
                        const float glow = rng.f();
                        const float podH = rng.range(4.0f, 9.0f);
                        // podium
                        box(solid, fp.x0 - 1.0f, fp.x1 + 1.0f, 0.0f, podH, fp.z0 - 1.0f, fp.z1 + 1.0f,
                            lerp(col, vec3(0.5f, 0.5f, 0.5f), 0.25f), 0.8f, glow);
                        footprints.push_back(Rect{ fp.x0 - 1.0f, fp.x1 + 1.0f, fp.z0 - 1.0f, fp.z1 + 1.0f });
                        addBlocker(blockers, fp.x0 - 1.0f, fp.x1 + 1.0f, fp.z0 - 1.0f, fp.z1 + 1.0f);
                        // shaft
                        box(solid, fp.x0, fp.x1, 0.0f, h, fp.z0, fp.z1, col, 1.0f, glow);
                        addBlocker(blockers, fp.x0, fp.x1, fp.z0, fp.z1);
                        ++buildingCount;
                        if (rng.chance(0.6f))             // setback crown
                        {
                            const float sw = w * 0.62f, sd = d * 0.62f;
                            box(solid, bx - sw * 0.5f, bx + sw * 0.5f, h, h + rng.range(6.0f, 24.0f),
                                bz - sd * 0.5f, bz + sd * 0.5f, col, 1.0f, glow);
                        }
                        box(solid, bx - 2.4f, bx + 2.4f, h, h + 3.2f, bz - 2.4f, bz + 2.4f,
                            vec3(0.34f, 0.35f, 0.37f), 0.0f, glow, 0.1f);
                        if (h > 95.0f && rng.chance(0.55f))
                            box(solid, bx - 0.45f, bx + 0.45f, h + 3.2f, h + rng.range(12.0f, 30.0f),
                                bz - 0.45f, bz + 0.45f, C_METAL, 0.0f, 0.0f, 0.1f);
                    }
                }
            }
            else if (district == 1 || district == 2)
            {
                const float half = TW_BLOCK * 0.5f;
                for (int q = 0; q < 4; ++q)
                {
                    const int qx = q % 2, qz = q / 2;
                    const float qx0 = lot.x0 + half * (float)qx;
                    const float qx1 = lot.x0 + half * (float)(qx + 1);
                    const float qz0 = lot.z0 + half * (float)qz;
                    const float qz1 = lot.z0 + half * (float)(qz + 1);
                    const float qcx = (qx0 + qx1) * 0.5f, qcz = (qz0 + qz1) * 0.5f;
                    if (!rng.chance(district == 1 ? 0.86f : 0.72f))
                    {
                        tryTree(Rect{ qx0, qx1, qz0, qz1 }, rng, 0.9f, 1.5f);
                        continue;
                    }
                    if (district == 1)
                    {
                        const float w = rng.range(13.0f, 19.0f), d = rng.range(13.0f, 19.0f);
                        const float h = rng.range(16.0f, 58.0f);
                        const float bx = qcx + rng.range(-1.0f, 1.0f) * (half - w) * 0.35f;
                        const float bz = qcz + rng.range(-1.0f, 1.0f) * (half - d) * 0.35f;
                        Rect fp{ bx - w * 0.5f, bx + w * 0.5f, bz - d * 0.5f, bz + d * 0.5f };
                        if (canPlace(lot, fp))
                            building(bx, bz, w, d, h, pick(MIDC, rng), 1.0f, rng.chance(0.15f), rng);
                    }
                    else
                    {
                        const float w = rng.range(8.0f, 14.0f), d = rng.range(8.0f, 14.0f);
                        const float h = rng.range(6.0f, 13.0f);
                        const float bx = qcx + rng.range(-1.0f, 1.0f) * (half - w) * 0.40f;
                        const float bz = qcz + rng.range(-1.0f, 1.0f) * (half - d) * 0.40f;
                        Rect fp{ bx - w * 0.5f, bx + w * 0.5f, bz - d * 0.5f, bz + d * 0.5f };
                        if (canPlace(lot, fp))
                            building(bx, bz, w, d, h, pick(HOUSEC, rng), 0.5f, rng.chance(0.7f), rng);
                        else
                            tryTree(Rect{ qx0, qx1, qz0, qz1 }, rng, 0.9f, 1.5f);
                    }
                }
                const int nExtra = district == 1 ? rng.irange(0, 2) : rng.irange(1, 4);
                for (int t = 0; t < nExtra; ++t) tryTree(lot, rng, 0.8f, 1.6f);
            }
            else
            {
                // fringe: scattered houses and lots of trees
                const int nH = rng.irange(0, 2);
                for (int t = 0; t < nH; ++t)
                {
                    const float w = rng.range(8.0f, 13.0f), d = rng.range(8.0f, 13.0f);
                    const float h = rng.range(5.0f, 10.0f);
                    const float bx = rng.range(lot.x0 + w * 0.5f + 1.0f, lot.x1 - w * 0.5f - 1.0f);
                    const float bz = rng.range(lot.z0 + d * 0.5f + 1.0f, lot.z1 - d * 0.5f - 1.0f);
                    Rect fp{ bx - w * 0.5f, bx + w * 0.5f, bz - d * 0.5f, bz + d * 0.5f };
                    if (canPlace(lot, fp))
                        building(bx, bz, w, d, h, pick(HOUSEC, rng), 0.45f, rng.chance(0.8f), rng);
                }
                const int nT = rng.irange(2, 6);
                for (int t = 0; t < nT; ++t) tryTree(lot, rng, 0.9f, 1.9f);
            }
        }
    }

    // ---- streets: lattice, cut by the rivers and by each other ------------
    std::vector<Rect> streetCuts = waterCuts;
    for (int i = 0; i < TW_GRID; ++i)
    {
        Rect r{ cor(i), cor(i) + TW_ROAD, -TW_CITY_HALF, TW_CITY_HALF };
        std::vector<Rect> pieces = cutOut(r, streetCuts);
        for (size_t k = 0; k < pieces.size(); ++k)
            if (pieces[k].x1 - pieces[k].x0 > 0.4f && pieces[k].z1 - pieces[k].z0 > 0.4f)
                flatQuad(solid, pieces[k], TW_STREET_Y, C_ASPHALT);
        streetCuts.push_back(r);   // horizontal roads will not overlap these
    }
    for (int j = 0; j < TW_GRID; ++j)
    {
        Rect r{ -TW_CITY_HALF, TW_CITY_HALF, cor(j), cor(j) + TW_ROAD };
        std::vector<Rect> pieces = cutOut(r, streetCuts);
        for (size_t k = 0; k < pieces.size(); ++k)
            if (pieces[k].x1 - pieces[k].x0 > 0.4f && pieces[k].z1 - pieces[k].z0 > 0.4f)
                flatQuad(solid, pieces[k], TW_STREET_Y, C_ASPHALT);
    }

    // ---- street lighting near the centre ---------------------------------
    int lampCount = 0;
    for (int i = 0; i < TW_GRID; i += 2)
    {
        for (int j = 0; j < TW_GRID; j += 2)
        {
            const float px = cor(i) + 1.3f;
            const float pz = cor(j) + 1.3f;
            if (std::sqrt(px * px + pz * pz) > 340.0f) continue;
            if (px > riverV.x0 - 1.0f && px < riverV.x1 + 1.0f) continue;
            if (pz > riverH.z0 - 1.0f && pz < riverH.z1 + 1.0f) continue;
            box(solid, px - 0.11f, px + 0.11f, 0.0f, 5.6f, pz - 0.11f, pz + 0.11f,
                C_METAL, 0.0f, 0.0f, 0.1f);
            box(solid, px - 0.11f, px + 0.11f, 5.35f, 5.6f, pz, pz + 1.2f, C_METAL, 0.0f, 0.0f, 0.1f);
            box(solid, px - 0.30f, px + 0.30f, 5.12f, 5.36f, pz + 0.9f, pz + 1.5f,
                C_LAMP, 0.0f, 0.0f, 0.0f);
            addBlocker(blockers, px - 0.22f, px + 0.22f, pz - 0.22f, pz + 0.22f);
            ++lampCount;
        }
    }

    // ---- clouds: soft quads, brighter on top, shaded underneath -----------
    {
        Rng cr(seed ^ 0xC10Du);
        const int puffCount = 90;
        for (int k = 0; k < puffCount; ++k)
        {
            const float bx = cr.range(-880.0f, 880.0f);
            const float bz = cr.range(-880.0f, 880.0f);
            const float by = cr.range(205.0f, 300.0f);
            const float tint = cr.range(0.86f, 1.0f);
            const int puffs = cr.irange(2, 4);
            for (int p = 0; p < puffs; ++p)
            {
                const float px = bx + cr.range(-70.0f, 70.0f);
                const float pz = bz + cr.range(-60.0f, 60.0f);
                const float py = by + cr.range(-12.0f, 12.0f);
                const float rx = cr.range(38.0f, 86.0f);
                const float rz = cr.range(30.0f, 68.0f);
                const float a = cr.range(0.55f, 0.85f);
                const vec3 top = vec3(0.98f * tint, 0.98f * tint, 1.0f * tint);
                const vec3 bot = vec3(0.58f * tint, 0.60f * tint, 0.68f * tint);
                const vec3 c0 = vec3(px - rx, py + 2.5f, pz - rz);
                const vec3 c1 = vec3(px + rx, py + 2.5f, pz - rz);
                const vec3 c2 = vec3(px + rx, py + 2.5f, pz + rz);
                const vec3 c3 = vec3(px - rx, py + 2.5f, pz + rz);
                const vec3 cc = vec3(px, py + 2.5f, pz);
                for (int s = 0; s < 4; ++s)
                {
                    const vec3 a0 = s == 0 ? c0 : (s == 1 ? c1 : (s == 2 ? c2 : c3));
                    const vec3 a1 = s == 0 ? c1 : (s == 1 ? c2 : (s == 2 ? c3 : c0));
                    const vec3* tri[3] = { &cc, &a0, &a1 };
                    for (int t = 0; t < 3; ++t)
                    {
                        const vec3& q = *tri[t];
                        CloudVertex cv;
                        cv.x = q.x; cv.y = q.y; cv.z = q.z;
                        cv.r = top.x; cv.g = top.y; cv.b = top.z;
                        cv.a = t == 0 ? a : a * 0.15f;
                        clouds.push_back(cv);
                    }
                    // underside, pushed down so the shaded face is what you see
                    for (int t = 0; t < 3; ++t)
                    {
                        const vec3& q = *tri[t];
                        CloudVertex cv;
                        cv.x = q.x; cv.y = q.y - 5.0f; cv.z = q.z;
                        cv.r = bot.x; cv.g = bot.y; cv.b = bot.z;
                        cv.a = t == 0 ? a * 0.95f : a * 0.12f;
                        clouds.push_back(cv);
                    }
                }
            }
        }
    }

    // ---- spawn: on a street in the suburbs, looking at the skyline -------
    spawn = vec3(cor(4) + TW_ROAD * 0.5f, 1.7f, cor(11) + TW_ROAD * 0.5f);
    if (blockedAt(spawn.x, spawn.z))            // a lamp or bank shouldn't be here, but never spawn stuck
    {
        bool free = false;
        for (int s = 1; s <= 60 && !free; ++s)
            for (int k = 0; k < 2 && !free; ++k)
            {
                const float z = spawn.z + (k == 0 ? 2.0f * (float)s : -2.0f * (float)s);
                if (fabsf(z) < TW_WALK_LIMIT && !blockedAt(spawn.x, z)) { spawn.z = z; free = true; }
            }
    }
    spawnYaw = std::atan2(-spawn.x, spawn.z);

    printf("[world] seed=%u buildings=%d trees=%d lamps=%d bridges=%d\n",
           seed, buildingCount, treeCount, lampCount, (int)bridges.size());
    printf("[world] solid=%zu tris  water=%zu tris  clouds=%zu tris  blockers=%zu  waterRects=%zu  bridgeZones=%zu\n",
           solid.size() / 3, water.size() / 3, clouds.size() / 3,
           blockers.size(), waterRects.size(), bridgeZones.size());
}

// ---------------------------------------------------------------------------
// collision: circle (radius TW_PLAYER_R) against the AABB list
// ---------------------------------------------------------------------------
bool World::blockedAt(float x, float z) const
{
    const float r = TW_PLAYER_R;
    if (x < -TW_WALK_LIMIT || x > TW_WALK_LIMIT || z < -TW_WALK_LIMIT || z > TW_WALK_LIMIT)
        return true;

    for (size_t k = 0; k < blockers.size(); ++k)
    {
        const AABB& b = blockers[k];
        if (x > b.minX - r && x < b.maxX + r && z > b.minZ - r && z < b.maxZ + r) return true;
    }
    for (size_t k = 0; k < waterRects.size(); ++k)
    {
        const AABB& w = waterRects[k];
        if (x > w.minX - r && x < w.maxX + r && z > w.minZ - r && z < w.maxZ + r)
        {
            bool onBridge = false;
            for (size_t q = 0; q < bridgeZones.size(); ++q)
            {
                const AABB& bz = bridgeZones[q];
                if (x > bz.minX && x < bz.maxX && z > bz.minZ && z < bz.maxZ) { onBridge = true; break; }
            }
            if (!onBridge) return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// What is under your feet
// ---------------------------------------------------------------------------
// This town has five walkable heights and they differ by only 18 cm, so they
// cannot be guessed: assume one level and pedestrians sink into the asphalt
// (streets are 5 cm proud of the blocks) or wade through a bridge deck (12 cm).
// Order matters. The deck slab outranks the kerbs it crosses.
float World::surfaceAt(float x, float z) const
{
    const Cor cor;

    // Bridge deck slab. bridgeZones is inset from the slab by the railing
    // thickness, so inflate back out to the footprint actually drawn.
    for (size_t q = 0; q < bridgeZones.size(); ++q)
    {
        const AABB& b = bridgeZones[q];
        if (x > b.minX - TW_RAIL_T && x < b.maxX + TW_RAIL_T &&
            z > b.minZ - TW_RAIL_T && z < b.maxZ + TW_RAIL_T) return TW_DECK_Y;
    }

    // River kerbs: a coping of TW_QUAY_W on each bank of each river, running
    // the whole river length. Where a deck crosses, the wall helper leaves a
    // gap and the test above has already answered.
    const float vx = cor(TW_RIVER_V) + TW_ROAD * 0.5f;
    const float hz = cor(TW_RIVER_H) + TW_ROAD * 0.5f;
    if (std::fabs(std::fabs(x - vx) - (TW_RIVER_VH + TW_QUAY_W * 0.5f)) < TW_QUAY_W * 0.5f) return TW_QUAY_Y;
    if (std::fabs(std::fabs(z - hz) - (TW_RIVER_HH + TW_QUAY_W * 0.5f)) < TW_QUAY_W * 0.5f) return TW_QUAY_Y;

    // Inside the street lattice. Streets are corridor i = [cor(i), cor(i)+ROAD],
    // repeating every cell, so corridor membership is a modulo test.
    if (std::fabs(x) <= TW_CITY_HALF && std::fabs(z) <= TW_CITY_HALF)
    {
        const float tx = std::fmod(x + TW_CITY_HALF, TW_CELL);
        const float tz = std::fmod(z + TW_CITY_HALF, TW_CELL);
        if (tx < TW_ROAD || tz < TW_ROAD) return TW_STREET_Y;
        return TW_LOT_Y;
    }

    return TW_FIELD_Y;   // open ground beyond the grid
}

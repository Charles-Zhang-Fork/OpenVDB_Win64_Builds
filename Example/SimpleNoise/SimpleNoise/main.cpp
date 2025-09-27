#include <openvdb/openvdb.h>
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

// ----- tiny 3D value noise (deterministic, no deps) -----
static inline uint32_t hash3(int x, int y, int z, uint32_t seed = 1337u) {
    uint32_t h = seed;
    h ^= 374761393u + 0x9E3779B9u * (uint32_t)x;
    h = (h ^ (h >> 16)) * 2654435761u;
    h ^= 0x85EBCA6Bu + 0x9E3779B9u * (uint32_t)y;
    h = (h ^ (h >> 13)) * 2246822519u;
    h ^= 0xC2B2AE35u + 0x9E3779B9u * (uint32_t)z;
    h ^= h >> 16;
    return h;
}
static inline float rnd(int x, int y, int z) {
    return (hash3(x, y, z) & 0xFFFFFF) / float(0xFFFFFF); // [0,1]
}
static inline float lerp(float a, float b, float t) { return a + t * (b - a); }
static inline float smooth(float t) { return t * t * (3.f - 2.f * t); } // smoothstep

// lattice value noise with trilinear interp
static float valueNoise3(float x, float y, float z) {
    int X = (int)std::floor(x), Y = (int)std::floor(y), Z = (int)std::floor(z);
    float fx = smooth(x - X), fy = smooth(y - Y), fz = smooth(z - Z);
    float c000 = rnd(X, Y, Z);
    float c100 = rnd(X + 1, Y, Z);
    float c010 = rnd(X, Y + 1, Z);
    float c110 = rnd(X + 1, Y + 1, Z);
    float c001 = rnd(X, Y, Z + 1);
    float c101 = rnd(X + 1, Y, Z + 1);
    float c011 = rnd(X, Y + 1, Z + 1);
    float c111 = rnd(X + 1, Y + 1, Z + 1);
    float x00 = lerp(c000, c100, fx), x10 = lerp(c010, c110, fx);
    float x01 = lerp(c001, c101, fx), x11 = lerp(c011, c111, fx);
    float y0 = lerp(x00, x10, fy), y1 = lerp(x01, x11, fy);
    return lerp(y0, y1, fz); // [0,1]
}

// fractal Brownian motion: sum of octaves
static float fbm(float x, float y, float z, int octaves = 5, float lacunarity = 2.0f, float gain = 0.5f) {
    float amp = 0.5f, freq = 1.0f, sum = 0.0f, norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * valueNoise3(x * freq, y * freq, z * freq);
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return sum / std::max(norm, 1e-6f); // [0,1]
}

int main() {
    openvdb::initialize();

    // Grid setup
    const int NX = 256, NY = 192, NZ = 256;           // volume resolution
    const float voxelSize = 0.01f;              // world units per voxel
    auto grid = openvdb::FloatGrid::create(/*background*/0.0f);
    grid->setName("density");
    grid->setTransform(openvdb::math::Transform::createLinearTransform(voxelSize));

    // Fill a box region with fractal noise
    openvdb::FloatGrid::Accessor acc = grid->getAccessor();

    // model params
    const float baseFreq = 0.015f;              // spatial frequency
    const float densityScale = 1.7f;            // boost contrast
    const float threshold = 0.02f;              // activate only meaningful voxels

    for (int z = 0; z < NZ; ++z) {
        for (int y = 0; y < NY; ++y) {
            for (int x = 0; x < NX; ++x) {
                // center the domain and sample fbm
                float sx = (x - NX * 0.5f) * baseFreq;
                float sy = (y - NY * 0.5f) * baseFreq;
                float sz = (z - NZ * 0.5f) * baseFreq;
                float n = fbm(sx, sy, sz, /*octaves*/6, /*lacunarity*/2.0f, /*gain*/0.5f);

                // shape it a bit (soft spherical falloff)
                float dx = (x - NX * 0.5f) / (NX * 0.5f), dy = (y - NY * 0.5f) / (NY * 0.5f), dz = (z - NZ * 0.5f) / (NZ * 0.5f);
                float r2 = dx * dx + dy * dy + dz * dz;
                float falloff = std::clamp(1.0f - r2, 0.0f, 1.0f);

                float density = std::clamp((n * falloff) * densityScale, 0.0f, 1.0f);

                if (density > threshold) {
                    acc.setValueOn(openvdb::Coord(x, y, z), density);
                }
            }
        }
    }

    // Write .vdb
    openvdb::io::File file("cloud.vdb");
    openvdb::GridPtrVec grids; grids.push_back(grid);
    file.write(grids);
    file.close();

    return 0;
}

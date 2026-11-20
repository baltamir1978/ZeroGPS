/*
 * SPDX-License-Identifier: MIT
 */

#include "offline_zone.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace map {
namespace {

constexpr double kKmPerDegLat = 111.32;
constexpr double kPi = 3.14159265358979323846;

BBox normalized(BBox box) {
    if (box.min_lat > box.max_lat) {
        std::swap(box.min_lat, box.max_lat);
    }
    if (box.min_lon > box.max_lon) {
        std::swap(box.min_lon, box.max_lon);
    }
    return box;
}

} // namespace

const std::vector<ZonePreset>& zone_presets() {
    static const std::vector<ZonePreset> presets = {
        {"Sierra de Guadarrama", {40.62, -4.30, 41.02, -3.65}},
        {"Sierra de Gredos", {40.12, -5.70, 40.45, -4.80}},
        {"Picos de Europa", {43.03, -5.15, 43.32, -4.50}},
        {"Sierra Nevada", {36.92, -3.65, 37.22, -2.80}},
        {"Pirineo central", {42.45, -0.60, 42.85, 0.90}},
        {"Sierra de Cazorla", {37.65, -3.15, 38.20, -2.55}},
        {"Montseny", {41.70, 2.25, 41.87, 2.55}},
        {"Madrid ciudad", {40.31, -3.84, 40.56, -3.52}},
    };
    return presets;
}

BBox bbox_around(double lat, double lon, double radius_km) {
    const double dlat = radius_km / kKmPerDegLat;
    const double cos_lat = std::max(0.01, std::cos(lat * kPi / 180.0));
    const double dlon = radius_km / (kKmPerDegLat * cos_lat);
    return BBox{lat - dlat, lon - dlon, lat + dlat, lon + dlon};
}

long long count_tiles(const BBox& raw, int z) {
    const BBox box = normalized(raw);
    const Tile tl = tile_at(box.max_lat, box.min_lon, z);
    const Tile br = tile_at(box.min_lat, box.max_lon, z);
    return static_cast<long long>(br.x - tl.x + 1) * static_cast<long long>(br.y - tl.y + 1);
}

long long estimated_tile_bytes(int z) {
    // Medido: z12 ≈ 25,8 KB, z13-14 ≈ 20,9 KB. A zooms altos hay más tesela «vacía» (campo)
    // y a bajos más detalle por píxel; se deja un margen del ~10 %.
    if (z <= 10) {
        return 30 * 1024;
    }
    if (z <= 12) {
        return 28 * 1024;
    }
    return 23 * 1024;
}

ZonePlan plan_zone(const BBox& box, int zmin, int zmax) {
    ZonePlan plan;
    zmin = std::clamp(zmin, kDownloadZoomMin, kDownloadZoomMax);
    zmax = std::clamp(zmax, zmin, kDownloadZoomMax);
    plan.zmin = zmin;
    plan.zmax = zmax;
    for (int z = zmin; z <= zmax; ++z) {
        const long long n = count_tiles(box, z);
        plan.per_zoom[z] = n;
        plan.tiles += n;
        plan.bytes += n * estimated_tile_bytes(z);
    }
    return plan;
}

std::vector<Tile> zone_tiles(const BBox& box, int zmin, int zmax) {
    std::vector<Tile> out;
    const ZonePlan plan = plan_zone(box, zmin, zmax);
    out.reserve(static_cast<std::size_t>(plan.tiles));
    for (int z = plan.zmin; z <= plan.zmax; ++z) {
        const auto tiles = tiles_for_bbox(box.min_lat, box.min_lon, box.max_lat, box.max_lon, z);
        out.insert(out.end(), tiles.begin(), tiles.end());
    }
    return out;
}

int fit_zoom(const BBox& raw, int w, int h, int zmin, int zmax) {
    const BBox box = normalized(raw);
    for (int z = zmax; z > zmin; --z) {
        const double px_w = (lon_to_tile_x(box.max_lon, z) - lon_to_tile_x(box.min_lon, z)) * kTileSize;
        const double px_h = (lat_to_tile_y(box.min_lat, z) - lat_to_tile_y(box.max_lat, z)) * kTileSize;
        if (px_w <= w && px_h <= h) {
            return z;
        }
    }
    return zmin;
}

std::string format_bytes(long long bytes) {
    char buf[32];
    const double kb = bytes / 1024.0;
    const double mb = kb / 1024.0;
    const double gb = mb / 1024.0;
    if (gb >= 1.0) {
        std::snprintf(buf, sizeof(buf), "%.1f GB", gb);
    } else if (mb >= 10.0) {
        std::snprintf(buf, sizeof(buf), "%.0f MB", mb);
    } else if (mb >= 1.0) {
        std::snprintf(buf, sizeof(buf), "%.1f MB", mb);
    } else {
        std::snprintf(buf, sizeof(buf), "%.0f KB", std::max(0.0, kb));
    }
    std::string out = buf;
    std::replace(out.begin(), out.end(), '.', ',');
    return out;
}

std::string format_count(long long value) {
    const bool negative = value < 0;
    std::string digits = std::to_string(negative ? -value : value);
    std::string out;
    int count = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (count > 0 && count % 3 == 0) {
            out.push_back('.');
        }
        out.push_back(*it);
        ++count;
    }
    if (negative) {
        out.push_back('-');
    }
    std::reverse(out.begin(), out.end());
    return out;
}

} // namespace map

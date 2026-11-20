/*
 * SPDX-License-Identifier: MIT
 */

#include "map_tiles.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace map {
namespace {

constexpr double kPi = 3.14159265358979323846;

double clampd(double value, double lo, double hi) {
    return std::max(lo, std::min(hi, value));
}

int tile_count(int z) {
    return 1 << z; // 2^z
}

} // namespace

double lon_to_tile_x(double lon_deg, int z) {
    const double n = tile_count(z);
    return (lon_deg + 180.0) / 360.0 * n;
}

double lat_to_tile_y(double lat_deg, int z) {
    const double n = tile_count(z);
    // Web Mercator se define hasta ~±85.0511°; se recorta para no divergir en los polos.
    const double lat = clampd(lat_deg, -85.05112878, 85.05112878);
    const double lat_rad = lat * kPi / 180.0;
    return (1.0 - std::log(std::tan(lat_rad) + 1.0 / std::cos(lat_rad)) / kPi) / 2.0 * n;
}

double tile_x_to_lon(double x, int z) {
    const double n = tile_count(z);
    return x / n * 360.0 - 180.0;
}

double tile_y_to_lat(double y, int z) {
    const double n = tile_count(z);
    const double a = kPi * (1.0 - 2.0 * y / n);
    return std::atan(std::sinh(a)) * 180.0 / kPi;
}

Tile tile_at(double lat_deg, double lon_deg, int z) {
    const int n = tile_count(z);
    Tile tile;
    tile.z = z;
    tile.x = static_cast<int>(std::floor(lon_to_tile_x(lon_deg, z)));
    tile.y = static_cast<int>(std::floor(lat_to_tile_y(lat_deg, z)));
    tile.x = std::max(0, std::min(n - 1, tile.x));
    tile.y = std::max(0, std::min(n - 1, tile.y));
    return tile;
}

std::vector<Tile> tiles_for_bbox(double min_lat, double min_lon, double max_lat, double max_lon, int z) {
    if (min_lat > max_lat) {
        std::swap(min_lat, max_lat);
    }
    if (min_lon > max_lon) {
        std::swap(min_lon, max_lon);
    }
    // En Y, más al norte = menor índice de tesela: el mínimo sale de la latitud máxima.
    const Tile top_left = tile_at(max_lat, min_lon, z);
    const Tile bottom_right = tile_at(min_lat, max_lon, z);

    std::vector<Tile> tiles;
    for (int y = top_left.y; y <= bottom_right.y; ++y) {
        for (int x = top_left.x; x <= bottom_right.x; ++x) {
            tiles.push_back(Tile{z, x, y});
        }
    }
    return tiles;
}

std::string ign_mtn_url(const Tile& tile) {
    char buf[320];
    std::snprintf(buf, sizeof(buf),
                  "https://www.ign.es/wmts/mapa-raster?service=WMTS&request=GetTile&version=1.0.0"
                  "&layer=MTN&style=default&format=image/jpeg&tilematrixset=GoogleMapsCompatible"
                  "&tilematrix=%d&tilerow=%d&tilecol=%d",
                  tile.z, tile.y, tile.x);
    return buf;
}

std::string tile_cache_path(const Tile& tile) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%d/%d/%d.jpeg", tile.z, tile.x, tile.y);
    return buf;
}

double meters_per_pixel(double lat_deg, int z) {
    constexpr double kEarthCircumferenceM = 40075016.686;
    return kEarthCircumferenceM * std::cos(lat_deg * kPi / 180.0) / (kTileSize * static_cast<double>(tile_count(z)));
}

unsigned map_refresh_ms(int z) {
    if (z >= 16) {
        return 1000;
    }
    if (z >= 14) {
        return 2000;
    }
    if (z >= 12) {
        return 5000;
    }
    return 10000;
}

} // namespace map

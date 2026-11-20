/*
 * SPDX-License-Identifier: MIT
 */

#include "map_tiles.h"

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FALLO %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                  \
        }                                                                \
    } while (0)

bool near(double a, double b, double eps) {
    return std::fabs(a - b) < eps;
}

void test_round_trip() {
    const int z = 12;
    const double lon = -3.7038; // Madrid
    const double lat = 40.4168;
    CHECK(near(map::tile_x_to_lon(map::lon_to_tile_x(lon, z), z), lon, 1e-6));
    CHECK(near(map::tile_y_to_lat(map::lat_to_tile_y(lat, z), z), lat, 1e-4));
}

void test_tile_at_bounds() {
    const int z = 5;
    const int n = 1 << z;
    map::Tile tm = map::tile_at(40.4168, -3.7038, z);
    CHECK(tm.z == z && tm.x >= 0 && tm.x < n && tm.y >= 0 && tm.y < n);
    CHECK(map::tile_at(0.0, -180.0, z).x == 0);
    CHECK(map::tile_at(0.0, 179.999, z).x == n - 1);
    // El hemisferio norte tiene índices Y menores que el sur.
    CHECK(map::tile_at(60.0, 0.0, z).y < map::tile_at(-60.0, 0.0, z).y);
}

void test_bbox() {
    const int z = 14;
    const auto tiles = map::tiles_for_bbox(40.40, -3.71, 40.43, -3.69, z);
    CHECK(!tiles.empty());
    for (const auto& t : tiles) {
        CHECK(t.z == z);
    }
    // El número de teselas es un rectángulo completo.
    const map::Tile tl = map::tile_at(40.43, -3.71, z);
    const map::Tile br = map::tile_at(40.40, -3.69, z);
    const std::size_t expected =
        static_cast<std::size_t>(br.x - tl.x + 1) * static_cast<std::size_t>(br.y - tl.y + 1);
    CHECK(tiles.size() == expected);
}

void test_url_and_cache() {
    const map::Tile t{12, 2005, 1538};
    const std::string url = map::ign_mtn_url(t);
    CHECK(url.find("tilematrix=12") != std::string::npos);
    CHECK(url.find("tilecol=2005") != std::string::npos);
    CHECK(url.find("tilerow=1538") != std::string::npos);
    CHECK(map::tile_cache_path(t) == "12/2005/1538.jpeg");
}

} // namespace

int main() {
    test_round_trip();
    test_tile_at_bounds();
    test_bbox();
    test_url_and_cache();
    if (failures == 0) {
        std::printf("map_tiles_test: OK\n");
    }
    return failures == 0 ? 0 : 1;
}

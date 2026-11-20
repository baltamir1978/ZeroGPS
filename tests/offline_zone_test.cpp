/*
 * SPDX-License-Identifier: MIT
 */

#include "offline_zone.h"
#include "storage_mounts.h"

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

void test_bbox_around() {
    const map::BBox b = map::bbox_around(40.0, -4.0, 10.0);
    CHECK(b.min_lat < 40.0 && b.max_lat > 40.0);
    CHECK(std::fabs((b.max_lat - b.min_lat) - 20.0 / 111.32) < 1e-6);
    // A 40° un grado de longitud es más corto: el cuadrado es más ancho en grados.
    CHECK((b.max_lon - b.min_lon) > (b.max_lat - b.min_lat));
}

void test_count_matches_list() {
    const map::BBox b = map::bbox_around(40.8, -4.0, 5.0);
    for (int z = 8; z <= 15; ++z) {
        const auto tiles = map::tiles_for_bbox(b.min_lat, b.min_lon, b.max_lat, b.max_lon, z);
        CHECK(map::count_tiles(b, z) == static_cast<long long>(tiles.size()));
    }
}

void test_plan() {
    const map::BBox b = map::bbox_around(40.8, -4.0, 5.0);
    const map::ZonePlan p = map::plan_zone(b, 10, 14);
    long long sum = 0;
    for (int z = 10; z <= 14; ++z) {
        sum += p.per_zoom[z];
    }
    CHECK(p.tiles == sum);
    CHECK(p.bytes > p.tiles * 20 * 1024);
    CHECK(static_cast<long long>(map::zone_tiles(b, 10, 14).size()) == p.tiles);
    // Cada zoom tiene unas cuatro veces (menos por los bordes) las teselas del anterior.
    CHECK(p.per_zoom[14] > 2 * p.per_zoom[13]);
    // Los zooms fuera de rango se recortan.
    const map::ZonePlan q = map::plan_zone(b, 1, 25);
    CHECK(q.zmin == map::kDownloadZoomMin && q.zmax == 17); // el topográfico llega a 17
    const map::ZonePlan o = map::plan_zone(b, 1, 25, map::MapLayer::Ortho);
    CHECK(o.zmax == 19);
    // La misma zona en satélite pesa menos que en topográfico, y en Base menos aún.
    const map::ZonePlan t15 = map::plan_zone(b, 10, 15, map::MapLayer::Topo);
    const map::ZonePlan o15 = map::plan_zone(b, 10, 15, map::MapLayer::Ortho);
    const map::ZonePlan b15 = map::plan_zone(b, 10, 15, map::MapLayer::Base);
    CHECK(t15.tiles == o15.tiles && o15.tiles == b15.tiles);
    CHECK(t15.bytes > o15.bytes && o15.bytes > b15.bytes);
    // zone_tiles va de zoom menor a mayor.
    const auto tiles = map::zone_tiles(b, 10, 12);
    CHECK(tiles.front().z == 10 && tiles.back().z == 12);
}

void test_presets_reasonable() {
    // Las zonas ya hechas tienen que caber de sobra en el tope hasta zoom 15.
    for (const auto& preset : map::zone_presets()) {
        const map::ZonePlan p = map::plan_zone(preset.box, 6, 15);
        CHECK(p.tiles > 0 && p.tiles < map::kMaxTilesPerDownload);
    }
}

void test_fit_zoom() {
    const map::BBox b = map::bbox_around(40.8, -4.0, 1.0); // 2 km
    const int z = map::fit_zoom(b, 320, 140, 5, 17);
    CHECK(z >= 12 && z <= 14);
    CHECK(map::fit_zoom(b, 320, 140, 5, 10) == 10);
}

void test_format() {
    CHECK(map::format_count(0) == "0");
    CHECK(map::format_count(999) == "999");
    CHECK(map::format_count(12345) == "12.345");
    CHECK(map::format_count(1234567) == "1.234.567");
    CHECK(map::format_bytes(512 * 1024) == "512 KB");
    CHECK(map::format_bytes(3 * 1024 * 1024 + 300 * 1024) == "3,3 MB");
    CHECK(map::format_bytes(250LL * 1024 * 1024) == "250 MB");
    CHECK(map::format_bytes(1536LL * 1024 * 1024) == "1,5 GB");
}

void test_refresh_by_zoom() {
    CHECK(map::map_refresh_ms(17) < map::map_refresh_ms(14));
    CHECK(map::map_refresh_ms(14) < map::map_refresh_ms(12));
    CHECK(map::map_refresh_ms(12) < map::map_refresh_ms(8));
    // A z17 un píxel ronda el metro en España.
    const double m = map::meters_per_pixel(40.4, 17);
    CHECK(m > 0.8 && m < 1.0);
}

void test_layers() {
    const map::Tile t{14, 8004, 6150};
    CHECK(map::tile_path(map::MapLayer::Topo, t) == "14/8004/6150.jpeg");
    CHECK(map::tile_path(map::MapLayer::Ortho, t) == "pnoa/14/8004/6150.jpeg");
    CHECK(map::tile_path(map::MapLayer::Base, t) == "base/14/8004/6150.jpeg");
    // La URL del topográfico es la de siempre.
    CHECK(map::tile_url(map::MapLayer::Topo, t) == map::ign_mtn_url(t));
    CHECK(map::tile_url(map::MapLayer::Ortho, t).find("pnoa-ma?") != std::string::npos);
    CHECK(map::tile_url(map::MapLayer::Ortho, t).find("layer=OI.OrthoimageCoverage") != std::string::npos);
    CHECK(map::tile_url(map::MapLayer::Base, t).find("layer=IGNBaseTodo") != std::string::npos);
    map::MapLayer l = map::MapLayer::Topo;
    CHECK(map::layer_from_key("pnoa", l) && l == map::MapLayer::Ortho);
    CHECK(!map::layer_from_key("osm", l) && l == map::MapLayer::Ortho);
    CHECK(map::next_layer(map::MapLayer::Base) == map::MapLayer::Topo);
}

void test_mounts() {
    const std::string text =
        "/dev/mmcblk0p2 / ext4 rw,noatime 0 0\n"
        "proc /proc proc rw 0 0\n"
        "/dev/sda1 /media/bruno/MI\\040SD vfat rw,nosuid 0 0\n"
        "/dev/sdb1 /mnt/datos exfat rw 0 0\n"
        "tmpfs /mnt/ram tmpfs rw 0 0\n"
        "/dev/sdc1 /run/media/bruno/USB ext4 rw 0 0\n";
    const auto mounts = storage::removable_mounts(text);
    CHECK(mounts.size() == 3);
    CHECK(mounts.size() >= 1 && mounts[0] == "/media/bruno/MI SD");
    CHECK(mounts.size() >= 2 && mounts[1] == "/mnt/datos");
    CHECK(mounts.size() >= 3 && mounts[2] == "/run/media/bruno/USB");
}

} // namespace

int main() {
    test_bbox_around();
    test_count_matches_list();
    test_plan();
    test_presets_reasonable();
    test_fit_zoom();
    test_format();
    test_refresh_by_zoom();
    test_layers();
    test_mounts();
    if (failures == 0) {
        std::printf("offline_zone: todo bien\n");
    }
    return failures == 0 ? 0 : 1;
}

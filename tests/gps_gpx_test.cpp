/*
 * SPDX-License-Identifier: MIT
 */

#include "gps_gpx.h"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace {

int failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FALLO %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                  \
        }                                                                \
    } while (0)

bool near(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) < eps;
}

void test_round_trip() {
    gps::Track track;
    track.name = "Paseo";
    track.points.push_back({40.4168000, -3.7038000, 650.5, true, "2026-11-20T10:00:00Z"});
    track.points.push_back({40.4170000, -3.7040000, 0.0, false, ""});

    std::ostringstream out;
    std::string error;
    CHECK(gps::save_gpx(out, track, error));

    std::istringstream in(out.str());
    gps::Track loaded;
    CHECK(gps::load_gpx(in, loaded, error));
    CHECK(loaded.name == "Paseo");
    CHECK(loaded.points.size() == 2);
    CHECK(near(loaded.points[0].lat, 40.4168, 1e-6));
    CHECK(near(loaded.points[0].lon, -3.7038, 1e-6));
    CHECK(loaded.points[0].has_ele);
    CHECK(near(loaded.points[0].ele_m, 650.5, 1e-1));
    CHECK(loaded.points[0].time == "2026-11-20T10:00:00Z");
    CHECK(!loaded.points[1].has_ele);
}

void test_parse_handwritten() {
    const std::string text =
        "<?xml version=\"1.0\"?>\n"
        "<gpx version=\"1.1\"><trk><name>Ruta</name><trkseg>\n"
        "<trkpt lat=\"43.3619\" lon=\"-5.8494\"><ele>12.0</ele></trkpt>\n"
        "</trkseg></trk></gpx>\n";
    std::istringstream in(text);
    gps::Track track;
    std::string error;
    CHECK(gps::load_gpx(in, track, error));
    CHECK(track.name == "Ruta");
    CHECK(track.points.size() == 1);
    CHECK(near(track.points[0].lat, 43.3619, 1e-4));
    CHECK(near(track.points[0].lon, -5.8494, 1e-4));
    CHECK(track.points[0].has_ele);
}

void test_rejects_non_gpx() {
    std::istringstream in("esto no es un gpx");
    gps::Track track;
    std::string error;
    CHECK(!gps::load_gpx(in, track, error));
}

} // namespace

int main() {
    test_round_trip();
    test_parse_handwritten();
    test_rejects_non_gpx();
    if (failures == 0) {
        std::printf("gps_gpx_test: OK\n");
    }
    return failures == 0 ? 0 : 1;
}

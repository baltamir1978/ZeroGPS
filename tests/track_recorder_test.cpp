/*
 * SPDX-License-Identifier: MIT
 */

#include "track_recorder.h"

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

gps::Fix fix_at(double lat, double lon) {
    gps::Fix f;
    f.valid = true;
    f.lat = lat;
    f.lon = lon;
    f.fix_quality = 1;
    f.year = 2026;
    f.month = 11;
    f.day = 20;
    f.hour = 10;
    f.minute = 14;
    f.second = 3.4;
    return f;
}

void test_distance() {
    // Un grado de latitud ≈ 111,2 km.
    CHECK(std::fabs(gps::distance_m(40.0, -4.0, 41.0, -4.0) - 111195.0) < 100.0);
    CHECK(gps::distance_m(40.0, -4.0, 40.0, -4.0) == 0.0);
}

void test_recorder_filters() {
    gps::TrackRecorder rec(5.0, 30.0);
    rec.reset("prueba");
    gps::Fix invalid = fix_at(40.0, -4.0);
    invalid.valid = false;
    CHECK(!rec.offer(invalid, 0.0));
    CHECK(rec.offer(fix_at(40.0, -4.0), 0.0)); // primer punto
    // 1 m: ruido, no se guarda ni aunque pase el tiempo.
    CHECK(!rec.offer(fix_at(40.000009, -4.0), 1.0));
    CHECK(!rec.offer(fix_at(40.000009, -4.0), 100.0));
    // 3 m antes de 30 s: no; después de 30 s: sí (despacio, pero moviéndose).
    CHECK(!rec.offer(fix_at(40.000027, -4.0), 10.0));
    CHECK(rec.offer(fix_at(40.000027, -4.0), 40.0));
    // 10 m: sí, al momento.
    CHECK(rec.offer(fix_at(40.000117, -4.0), 41.0));
    CHECK(rec.track().points.size() == 3);
    CHECK(std::fabs(rec.length_m() - gps::track_length_m(rec.track())) < 1e-6);
    CHECK(rec.track().points[0].time == "2026-11-20T10:14:03Z");
}

void test_formats() {
    CHECK(gps::track_file_stem(fix_at(0, 0)) == "2026-11-20_101403");
    CHECK(gps::track_file_stem(gps::Fix{}).empty());
    CHECK(gps::format_duration(65) == "1:05");
    CHECK(gps::format_duration(3725) == "1:02:05");
    CHECK(gps::format_distance(850.4) == "850 m");
    CHECK(gps::format_distance(12345) == "12,3 km");
}

} // namespace

int main() {
    test_distance();
    test_recorder_filters();
    test_formats();
    if (failures == 0) {
        std::printf("track_recorder: todo bien\n");
    }
    return failures == 0 ? 0 : 1;
}

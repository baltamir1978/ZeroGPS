/*
 * SPDX-License-Identifier: MIT
 */

#include "gps_nmea.h"

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

bool near(double a, double b, double eps = 1e-4) {
    return std::fabs(a - b) < eps;
}

void test_gga() {
    gps::Parser parser;
    CHECK(parser.feed("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47"));
    const auto& fix = parser.fix();
    CHECK(near(fix.lat, 48.1173));
    CHECK(near(fix.lon, 11.516667));
    CHECK(near(fix.alt_m, 545.4, 1e-1));
    CHECK(fix.satellites == 8);
    CHECK(fix.fix_quality == 1);
    CHECK(fix.valid);
}

void test_rmc() {
    gps::Parser parser;
    CHECK(parser.feed("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A"));
    const auto& fix = parser.fix();
    CHECK(fix.valid);
    CHECK(near(fix.lat, 48.1173));
    CHECK(near(fix.lon, 11.516667));
    CHECK(near(fix.speed_kmh, 22.4 * 1.852, 1e-2));
    CHECK(fix.year == 1994 && fix.month == 3 && fix.day == 23);
    CHECK(fix.iso_time() == "1994-03-23T12:35:19Z");
}

void test_talker_and_checksum() {
    gps::Parser parser;
    // «Talker» GN (multiconstelación) debe valer igual que GP.
    CHECK(parser.feed("$GNGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*59"));
    // Checksum incorrecto: se rechaza.
    CHECK(!parser.feed("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*00"));
    // Basura: se rechaza.
    CHECK(!parser.feed("hola mundo"));
}

} // namespace

int main() {
    test_gga();
    test_rmc();
    test_talker_and_checksum();
    if (failures == 0) {
        std::printf("gps_nmea_test: OK\n");
    }
    return failures == 0 ? 0 : 1;
}

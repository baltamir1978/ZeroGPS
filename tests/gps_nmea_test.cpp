/*
 * SPDX-License-Identifier: MIT
 */

#include "gps_nmea.h"

#include <cmath>
#include <cstdio>
#include <string>

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

// "GPGSV,..." -> "$GPGSV,...*CS": así las pruebas no dependen de checksums a mano.
std::string nmea(const std::string& body) {
    unsigned cs = 0;
    for (char c : body) {
        cs ^= static_cast<unsigned char>(c);
    }
    char tail[8];
    std::snprintf(tail, sizeof(tail), "*%02X", cs);
    return "$" + body + tail;
}

const gps::Satellite* find(const gps::Fix& fix, gps::System system, int prn) {
    for (const auto& sat : fix.sky) {
        if (sat.system == system && sat.prn == prn) {
            return &sat;
        }
    }
    return nullptr;
}

void test_gsv_and_gsa() {
    gps::Parser parser;
    // Lo que manda el AT6668 recién encendido (copiado del aparato): sin elevación ni azimut.
    CHECK(parser.feed("$GPGSV,1,1,03,12,,,30,13,,,29,25,,,32,1*68"));
    CHECK(parser.feed("$GPGSA,A,1,,,,,,,,,,,,,25.5,25.5,25.5,1*1F"));
    CHECK(parser.fix().sky.size() == 3);
    const auto* s25 = find(parser.fix(), gps::System::Gps, 25);
    CHECK(s25 && s25->snr == 32 && s25->elevation == -1 && s25->azimuth == -1 && !s25->used);
    CHECK(parser.fix().fix_mode == 1);
    CHECK(near(parser.fix().hdop, 25.5));

    // GPS en dos partes (con id de señal), Galileo en una; la parte 2 cierra el ciclo.
    CHECK(parser.feed(nmea("GPGSV,2,1,05,02,45,120,40,05,10,300,,12,70,045,35,13,20,200,28,1")));
    CHECK(parser.fix().sky.size() == 3); // el ciclo nuevo aún no está completo
    CHECK(parser.feed(nmea("GPGSV,2,2,05,44,30,150,33,1")));
    CHECK(parser.feed(nmea("GAGSV,1,1,02,07,55,090,38,30,05,010,22,7")));
    CHECK(parser.feed(nmea("GNGSA,A,3,02,12,13,44,,,,,,,,,1.8,0.9,1.5,1")));
    CHECK(parser.feed(nmea("GNGSA,A,3,07,,,,,,,,,,,,1.8,0.9,1.5,3")));
    const auto& fix = parser.fix();
    CHECK(fix.sky.size() == 7);
    CHECK(fix.fix_mode == 3);
    CHECK(near(fix.pdop, 1.8) && near(fix.hdop, 0.9) && near(fix.vdop, 1.5));
    const auto* s02 = find(fix, gps::System::Gps, 2);
    CHECK(s02 && s02->used && s02->elevation == 45 && s02->azimuth == 120 && s02->snr == 40);
    const auto* s05 = find(fix, gps::System::Gps, 5);
    CHECK(s05 && !s05->used && s05->snr == -1);
    CHECK(find(fix, gps::System::Sbas, 44) && find(fix, gps::System::Sbas, 44)->used);
    CHECK(find(fix, gps::System::Galileo, 7) && find(fix, gps::System::Galileo, 7)->used);
    CHECK(find(fix, gps::System::Galileo, 30) && !find(fix, gps::System::Galileo, 30)->used);
    CHECK(!find(fix, gps::System::Gps, 25)); // el ciclo nuevo de GPS ya no lo trae
}

void test_gsv_signals_merge_and_expire() {
    gps::Parser parser;
    // El mismo satélite en L1 (señal 1) y L5 (señal 8): uno solo, con la mejor señal.
    CHECK(parser.feed(nmea("GPGSV,1,1,01,10,40,100,30,1")));
    CHECK(parser.feed(nmea("GPGSV,1,1,01,10,40,100,42,8")));
    CHECK(parser.feed(nmea("GLGSV,1,1,01,70,20,250,25,1")));
    CHECK(parser.fix().sky.size() == 2);
    CHECK(find(parser.fix(), gps::System::Gps, 10)->snr == 42);

    // Una parte perdida: el grupo no cambia hasta un ciclo completo.
    CHECK(parser.feed(nmea("GLGSV,2,2,05,71,20,250,25,1")));
    CHECK(find(parser.fix(), gps::System::Glonass, 70));

    // GLONASS deja de llegar: a las pocas ráfagas (GGA) se olvida; GPS se renueva y sigue.
    const std::string gga = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
    for (int i = 0; i < 8; ++i) {
        CHECK(parser.feed(nmea("GPGSV,1,1,01,10,40,100,30,1")));
        CHECK(parser.feed(nmea("GPGSV,1,1,01,10,40,100,42,8")));
        CHECK(parser.feed(gga));
    }
    CHECK(!find(parser.fix(), gps::System::Glonass, 70));
    CHECK(find(parser.fix(), gps::System::Gps, 10));
    CHECK(near(parser.fix().hdop, 0.9));
}

} // namespace

int main() {
    test_gsv_and_gsa();
    test_gsv_signals_merge_and_expire();
    test_gga();
    test_rmc();
    test_talker_and_checksum();
    if (failures == 0) {
        std::printf("gps_nmea_test: OK\n");
    }
    return failures == 0 ? 0 : 1;
}

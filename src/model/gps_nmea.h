/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <string>

namespace gps {

// Un «fix»: la posición y los datos que trae el GNSS en cada momento.
struct Fix {
    bool valid = false;   // RMC status 'A' / GGA quality > 0
    double lat = 0.0;     // grados (+N / -S)
    double lon = 0.0;     // grados (+E / -W)
    double alt_m = 0.0;   // altitud sobre el nivel del mar (GGA)
    double speed_kmh = 0.0;
    double course_deg = 0.0;
    int satellites = 0;
    int fix_quality = 0;

    // Fecha y hora UTC (RMC / GGA).
    int year = 0, month = 0, day = 0;
    int hour = 0, minute = 0;
    double second = 0.0;

    // Hora ISO 8601 UTC ("2026-11-20T10:14:03Z"), o "" si no hay fecha+hora.
    std::string iso_time() const;
};

// ¿El checksum (XOR entre '$' y '*') de una sentencia NMEA es correcto?
bool nmea_checksum_ok(const std::string& sentence);

/**
 * Va acumulando sentencias NMEA (RMC y GGA, de cualquier «talker»: GP, GN, GL…) y mantiene el
 * último fix. `feed` devuelve true si la línea era una sentencia válida que actualizó el fix.
 */
class Parser {
public:
    bool feed(const std::string& line);
    const Fix& fix() const {
        return fix_;
    }

private:
    Fix fix_;
};

} // namespace gps

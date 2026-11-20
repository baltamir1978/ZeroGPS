/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace gps {

// Constelaciones. El AT6668 es multiconstelación: cada una manda sus GSV con su «talker».
enum class System : uint8_t { Gps, Glonass, Galileo, Beidou, Qzss, Navic, Sbas, Unknown };

// Nombre corto para la pantalla ("GPS", "GLONASS", "Galileo"…).
const char* system_name(System system);

// Un satélite a la vista (GSV), marcado si se usa para la posición (GSA).
struct Satellite {
    System system = System::Unknown;
    int prn = 0;
    int elevation = -1; // grados sobre el horizonte; -1 si aún no se sabe (sin almanaque)
    int azimuth = -1;   // grados desde el norte; -1 si aún no se sabe
    int snr = -1;       // dB-Hz; -1 si no se recibe
    bool used = false;
};

// Un «fix»: la posición y los datos que trae el GNSS en cada momento.
struct Fix {
    bool valid = false;   // RMC status 'A' / GGA quality > 0
    double lat = 0.0;     // grados (+N / -S)
    double lon = 0.0;     // grados (+E / -W)
    double alt_m = 0.0;   // altitud sobre el nivel del mar (GGA)
    double speed_kmh = 0.0;
    double course_deg = 0.0;
    int satellites = 0;   // usados para la posición (GGA)
    int fix_quality = 0;
    int fix_mode = 0;     // GSA: 1 sin posición, 2 = 2D, 3 = 3D; 0 si no ha llegado
    double hdop = 0.0;    // dilución de precisión horizontal (GGA/GSA); 0 si no se sabe
    double vdop = 0.0;    // vertical (GSA)
    double pdop = 0.0;    // de posición (GSA)
    // El cielo: todos los satélites a la vista, ordenados por constelación y número.
    std::vector<Satellite> sky;

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
 * Va acumulando sentencias NMEA (RMC, GGA, GSA y GSV, de cualquier «talker»: GP, GN, GL…) y
 * mantiene el último fix. `feed` devuelve true si la línea era una sentencia válida que
 * actualizó el fix.
 *
 * El cielo (`Fix::sky`) se arma con los GSV, que llegan en varias partes por constelación (y
 * por señal, L1/L5…): un grupo sólo sustituye al anterior cuando llega completo. Una
 * constelación que deja de mandar GSV se olvida a los pocos segundos (cuenta de GGA/RMC).
 */
class Parser {
public:
    bool feed(const std::string& line);
    const Fix& fix() const {
        return fix_;
    }

private:
    struct GsvGroup {
        std::vector<Satellite> complete;
        std::vector<Satellite> pending;
        int expected = 0; // nº de partes del ciclo en curso
        int next = 1;     // la parte que toca
        long epoch = 0;
    };
    struct UsedSet {
        std::set<int> prns;
        long epoch = 0;
    };

    void parse_gsv(const std::vector<std::string>& t);
    void parse_gsa(const std::vector<std::string>& t);
    void rebuild_sky();

    Fix fix_;
    long epoch_ = 0; // sube con cada ráfaga (una por segundo)
    std::map<std::string, GsvGroup> gsv_; // clave: talker + id de señal
    std::map<System, UsedSet> used_;
};

} // namespace gps

/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "gps_gpx.h"
#include "gps_nmea.h"

#include <string>

namespace gps {

// Distancia sobre la esfera (haversine), en metros.
double distance_m(double lat1, double lon1, double lat2, double lon2);
double track_length_m(const Track& track);

// "2026-11-20_101403" a partir de la hora del fix (UTC), o "" si el fix no trae fecha.
std::string track_file_stem(const Fix& fix);

// "1:02:05" / "12:05".
std::string format_duration(double seconds);
// "850 m" / "12,3 km".
std::string format_distance(double meters);

/**
 * Decide qué fixes acaban en el track. Grabar cada segundo llenaría el GPX de puntos
 * repetidos (y de escrituras en la tarjeta): se guarda un punto cuando te has movido al menos
 * `min_distance_m`, o cada `max_interval_s` si te mueves despacio pero algo (más de 2 m),
 * para que el ruido del GNSS parado no dibuje una maraña.
 */
class TrackRecorder {
public:
    explicit TrackRecorder(double min_distance_m = 5.0, double max_interval_s = 30.0);

    void reset(std::string name);

    // `t_sec`: segundos de un reloj monótono. Devuelve true si el punto se añadió.
    bool offer(const Fix& fix, double t_sec);

    const Track& track() const {
        return track_;
    }
    double length_m() const {
        return length_m_;
    }

private:
    double min_distance_m_;
    double max_interval_s_;
    Track track_;
    double length_m_ = 0.0;
    double last_t_ = 0.0;
};

} // namespace gps

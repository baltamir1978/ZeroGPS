/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace gps {

struct TrackPoint {
    double lat = 0.0;
    double lon = 0.0;
    double ele_m = 0.0;
    bool has_ele = false;
    std::string time; // ISO 8601, o "" si no hay
};

struct Track {
    std::string name;
    std::vector<TrackPoint> points;

    bool empty() const {
        return points.empty();
    }
};

// Lectura y escritura de GPX 1.1 (un único <trk> con un <trkseg>). El lector es tolerante:
// busca los <trkpt lat= lon=> y sus <ele>/<time>, sin ser un parser XML completo.
bool save_gpx(std::ostream& out, const Track& track, std::string& error);
bool load_gpx(std::istream& in, Track& track, std::string& error);

bool save_gpx_file(const std::string& path, const Track& track, std::string& error);
bool load_gpx_file(const std::string& path, Track& track, std::string& error);

} // namespace gps

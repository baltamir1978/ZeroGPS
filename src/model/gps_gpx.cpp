/*
 * SPDX-License-Identifier: MIT
 */

#include "gps_gpx.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace gps {
namespace {

std::string escape_xml(const std::string& value) {
    std::string out;
    for (char c : value) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            default: out += c; break;
        }
    }
    return out;
}

// Valor de un atributo: nombre="valor".
std::string attribute(const std::string& text, const std::string& name) {
    const std::string needle = name + "=\"";
    const std::size_t start = text.find(needle);
    if (start == std::string::npos) {
        return "";
    }
    const std::size_t value_start = start + needle.size();
    const std::size_t value_end = text.find('"', value_start);
    if (value_end == std::string::npos) {
        return "";
    }
    return text.substr(value_start, value_end - value_start);
}

// Contenido de un elemento: <name>contenido</name>.
std::string element(const std::string& text, const std::string& name) {
    const std::string open = "<" + name + ">";
    const std::string close = "</" + name + ">";
    const std::size_t start = text.find(open);
    if (start == std::string::npos) {
        return "";
    }
    const std::size_t value_start = start + open.size();
    const std::size_t value_end = text.find(close, value_start);
    if (value_end == std::string::npos) {
        return "";
    }
    return text.substr(value_start, value_end - value_start);
}

} // namespace

bool save_gpx(std::ostream& out, const Track& track, std::string& error) {
    error.clear();
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<gpx version=\"1.1\" creator=\"ZeroGPS\" "
           "xmlns=\"http://www.topografix.com/GPX/1/1\">\n";
    out << "  <trk>\n";
    if (!track.name.empty()) {
        out << "    <name>" << escape_xml(track.name) << "</name>\n";
    }
    out << "    <trkseg>\n";
    char buf[64];
    for (const auto& p : track.points) {
        std::snprintf(buf, sizeof(buf), "%.7f", p.lat);
        std::string lat = buf;
        std::snprintf(buf, sizeof(buf), "%.7f", p.lon);
        out << "      <trkpt lat=\"" << lat << "\" lon=\"" << buf << "\">\n";
        if (p.has_ele) {
            std::snprintf(buf, sizeof(buf), "%.1f", p.ele_m);
            out << "        <ele>" << buf << "</ele>\n";
        }
        if (!p.time.empty()) {
            out << "        <time>" << p.time << "</time>\n";
        }
        out << "      </trkpt>\n";
    }
    out << "    </trkseg>\n  </trk>\n</gpx>\n";

    if (!out) {
        error = "write error";
        return false;
    }
    return true;
}

bool load_gpx(std::istream& in, Track& track, std::string& error) {
    error.clear();
    track = Track{};

    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string content = buffer.str();
    if (content.find("<gpx") == std::string::npos) {
        error = "not a GPX file";
        return false;
    }

    // Nombre de la pista: el primer <name> que aparezca antes del primer <trkpt>.
    const std::size_t first_pt = content.find("<trkpt");
    const std::size_t name_pos = content.find("<name>");
    if (name_pos != std::string::npos && (first_pt == std::string::npos || name_pos < first_pt)) {
        track.name = element(content, "name");
    }

    std::size_t pos = 0;
    while (true) {
        const std::size_t open = content.find("<trkpt", pos);
        if (open == std::string::npos) {
            break;
        }
        std::size_t end = content.find("</trkpt>", open);
        const std::size_t segment_end = (end == std::string::npos) ? content.size() : end;
        const std::string segment = content.substr(open, segment_end - open);

        TrackPoint point;
        point.lat = std::strtod(attribute(segment, "lat").c_str(), nullptr);
        point.lon = std::strtod(attribute(segment, "lon").c_str(), nullptr);
        const std::string ele = element(segment, "ele");
        if (!ele.empty()) {
            point.ele_m = std::strtod(ele.c_str(), nullptr);
            point.has_ele = true;
        }
        point.time = element(segment, "time");
        track.points.push_back(std::move(point));

        pos = (end == std::string::npos) ? content.size() : end + 8;
    }

    return true;
}

bool save_gpx_file(const std::string& path, const Track& track, std::string& error) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        error = "could not open file for writing";
        return false;
    }
    return save_gpx(out, track, error);
}

bool load_gpx_file(const std::string& path, Track& track, std::string& error) {
    std::ifstream in(path);
    if (!in) {
        error = "could not open file";
        return false;
    }
    return load_gpx(in, track, error);
}

} // namespace gps

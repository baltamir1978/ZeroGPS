/*
 * SPDX-License-Identifier: MIT
 */

#include "gps_nmea.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace gps {
namespace {

std::string trim(const std::string& value) {
    std::size_t begin = 0;
    std::size_t end = value.size();
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (begin < end && space(value[begin])) {
        ++begin;
    }
    while (end > begin && space(value[end - 1])) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::vector<std::string> split(const std::string& text, char sep) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t pos = text.find(sep, start);
        if (pos == std::string::npos) {
            out.push_back(text.substr(start));
            break;
        }
        out.push_back(text.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

double to_double(const std::string& s) {
    if (s.empty()) {
        return 0.0;
    }
    return std::strtod(s.c_str(), nullptr);
}

// "ddmm.mmmm" o "dddmm.mmmm" + hemisferio -> grados con signo. Los minutos son siempre los dos
// dígitos antes del punto, así que vale igual para latitud y longitud.
double coord_to_degrees(const std::string& field, const std::string& hemi) {
    if (field.empty()) {
        return 0.0;
    }
    const double value = to_double(field);
    const int degrees = static_cast<int>(value / 100.0);
    const double minutes = value - degrees * 100.0;
    double result = degrees + minutes / 60.0;
    if (hemi == "S" || hemi == "W") {
        result = -result;
    }
    return result;
}

void parse_time(const std::string& hhmmss, Fix& fix) {
    if (hhmmss.size() < 6) {
        return;
    }
    fix.hour = std::atoi(hhmmss.substr(0, 2).c_str());
    fix.minute = std::atoi(hhmmss.substr(2, 2).c_str());
    fix.second = to_double(hhmmss.substr(4));
}

void parse_date(const std::string& ddmmyy, Fix& fix) {
    if (ddmmyy.size() < 6) {
        return;
    }
    fix.day = std::atoi(ddmmyy.substr(0, 2).c_str());
    fix.month = std::atoi(ddmmyy.substr(2, 2).c_str());
    const int yy = std::atoi(ddmmyy.substr(4, 2).c_str());
    // Pivote de dos dígitos: 00-69 -> 2000-2069, 70-99 -> 1970-1999.
    fix.year = (yy < 70) ? 2000 + yy : 1900 + yy;
}

} // namespace

std::string Fix::iso_time() const {
    if (year == 0 || month == 0 || day == 0) {
        return "";
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", year, month, day, hour,
                  minute, static_cast<int>(second));
    return buf;
}

bool nmea_checksum_ok(const std::string& sentence) {
    const std::size_t star = sentence.find('*');
    const std::size_t dollar = sentence.find('$');
    if (dollar == std::string::npos || star == std::string::npos || star < dollar + 1 ||
        star + 2 >= sentence.size() + 1 || star + 3 > sentence.size()) {
        return false;
    }
    uint8_t checksum = 0;
    for (std::size_t i = dollar + 1; i < star; ++i) {
        checksum ^= static_cast<uint8_t>(sentence[i]);
    }
    const long given = std::strtol(sentence.substr(star + 1, 2).c_str(), nullptr, 16);
    return static_cast<long>(checksum) == given;
}

bool Parser::feed(const std::string& line) {
    const std::string sentence = trim(line);
    if (sentence.size() < 6 || sentence[0] != '$' || !nmea_checksum_ok(sentence)) {
        return false;
    }

    const std::string body = sentence.substr(0, sentence.find('*'));
    const std::vector<std::string> t = split(body, ',');
    if (t.empty() || t[0].size() < 6) {
        return false;
    }
    const std::string type = t[0].substr(3); // "$GPRMC" -> "RMC", "$GNGGA" -> "GGA"

    if (type == "RMC" && t.size() >= 10) {
        parse_time(t[1], fix_);
        fix_.valid = (t[2] == "A");
        if (!t[3].empty() && !t[5].empty()) {
            fix_.lat = coord_to_degrees(t[3], t[4]);
            fix_.lon = coord_to_degrees(t[5], t[6]);
        }
        fix_.speed_kmh = to_double(t[7]) * 1.852; // nudos -> km/h
        fix_.course_deg = to_double(t[8]);
        parse_date(t[9], fix_);
        return true;
    }

    if (type == "GGA" && t.size() >= 10) {
        parse_time(t[1], fix_);
        if (!t[2].empty() && !t[4].empty()) {
            fix_.lat = coord_to_degrees(t[2], t[3]);
            fix_.lon = coord_to_degrees(t[4], t[5]);
        }
        fix_.fix_quality = std::atoi(t[6].c_str());
        fix_.satellites = std::atoi(t[7].c_str());
        fix_.alt_m = to_double(t[9]);
        // Calidad 0 es «sin posición»: así se nota enseguida que se pierde la señal aunque
        // la RMC llegue después (o no llegue).
        fix_.valid = fix_.fix_quality > 0;
        return true;
    }

    return false;
}

} // namespace gps

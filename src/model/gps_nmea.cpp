/*
 * SPDX-License-Identifier: MIT
 */

#include "gps_nmea.h"

#include <algorithm>
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

int to_int(const std::string& s, int fallback) {
    if (s.empty()) {
        return fallback;
    }
    return std::atoi(s.c_str());
}

// Ráfagas (GGA) que aguanta un grupo de GSV o una lista de usados sin renovarse antes de
// olvidarlo: una constelación que se pierde no se queda pintada para siempre.
constexpr long kStaleEpochs = 5;

// Constelación por el «talker» ("GP", "GL"…). GN (combinado) no dice nada: se mira el número.
System system_for_talker(const std::string& talker) {
    if (talker == "GP") {
        return System::Gps;
    }
    if (talker == "GL") {
        return System::Glonass;
    }
    if (talker == "GA") {
        return System::Galileo;
    }
    if (talker == "GB" || talker == "BD") {
        return System::Beidou;
    }
    if (talker == "GQ" || talker == "QZ") {
        return System::Qzss;
    }
    if (talker == "GI") {
        return System::Navic;
    }
    return System::Unknown;
}

// Numeración «extendida» de NMEA 4.x, la que usan los receptores con talker GN.
System system_for_prn(int prn) {
    if (prn >= 1 && prn <= 32) {
        return System::Gps;
    }
    if (prn >= 33 && prn <= 64) {
        return System::Sbas;
    }
    if (prn >= 65 && prn <= 96) {
        return System::Glonass;
    }
    if (prn >= 193 && prn <= 199) {
        return System::Qzss;
    }
    if ((prn >= 201 && prn <= 263) || (prn >= 401 && prn <= 463)) {
        return System::Beidou;
    }
    if (prn >= 301 && prn <= 336) {
        return System::Galileo;
    }
    return System::Unknown;
}

// Los SBAS (EGNOS en España) llegan con talker GP y números 33-64 (o 120-158).
System resolve_system(System from_talker, int prn) {
    if (from_talker == System::Unknown) {
        return system_for_prn(prn);
    }
    if (from_talker == System::Gps && ((prn >= 33 && prn <= 64) || (prn >= 120 && prn <= 158))) {
        return System::Sbas;
    }
    return from_talker;
}

// Id de sistema del final de la GSA en NMEA 4.10+.
System system_for_id(const std::string& id) {
    switch (to_int(id, 0)) {
        case 1:
            return System::Gps;
        case 2:
            return System::Glonass;
        case 3:
            return System::Galileo;
        case 4:
            return System::Beidou;
        case 5:
            return System::Qzss;
        case 6:
            return System::Navic;
        default:
            return System::Unknown;
    }
}

} // namespace

const char* system_name(System system) {
    switch (system) {
        case System::Gps:
            return "GPS";
        case System::Glonass:
            return "GLONASS";
        case System::Galileo:
            return "Galileo";
        case System::Beidou:
            return "BeiDou";
        case System::Qzss:
            return "QZSS";
        case System::Navic:
            return "NavIC";
        case System::Sbas:
            return "SBAS";
        case System::Unknown:
        default:
            return "?";
    }
}

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
        if (!t[8].empty()) {
            fix_.hdop = to_double(t[8]);
        }
        // Una ráfaga por segundo trae una GGA: sirve de reloj para olvidar lo que no se renueva.
        ++epoch_;
        rebuild_sky();
        // Calidad 0 es «sin posición»: así se nota enseguida que se pierde la señal aunque
        // la RMC llegue después (o no llegue).
        fix_.valid = fix_.fix_quality > 0;
        return true;
    }

    if (type == "GSV" && t.size() >= 4) {
        parse_gsv(t);
        return true;
    }

    if (type == "GSA" && t.size() >= 18) {
        parse_gsa(t);
        return true;
    }

    return false;
}

void Parser::parse_gsv(const std::vector<std::string>& t) {
    // $xxGSV,total,parte,a_la_vista,{prn,elev,azim,snr}x1..4[,id_señal]
    const std::string talker = t[0].substr(1, 2);
    const int total = to_int(t[1], 0);
    const int part = to_int(t[2], 0);
    const std::size_t fields = t.size() - 4;
    const std::string signal = (fields % 4 == 1) ? t.back() : "";
    GsvGroup& group = gsv_[talker + signal];

    if (part == 1) {
        group.pending.clear();
        group.expected = total;
        group.next = 1;
    }
    if (part != group.next || total != group.expected) {
        // Se ha perdido una parte: se espera al ciclo siguiente.
        group.pending.clear();
        group.next = 1;
        return;
    }

    const System from_talker = system_for_talker(talker);
    for (std::size_t i = 4; i + 3 < t.size(); i += 4) {
        if (t[i].empty()) {
            continue;
        }
        Satellite sat;
        sat.prn = to_int(t[i], 0);
        sat.system = resolve_system(from_talker, sat.prn);
        sat.elevation = to_int(t[i + 1], -1);
        sat.azimuth = to_int(t[i + 2], -1);
        sat.snr = to_int(t[i + 3], -1);
        group.pending.push_back(sat);
    }
    ++group.next;

    if (part == total) {
        group.complete.swap(group.pending);
        group.pending.clear();
        group.next = 1;
        group.epoch = epoch_;
        rebuild_sky();
    }
}

void Parser::parse_gsa(const std::vector<std::string>& t) {
    // $xxGSA,modo,fix,prn x12,PDOP,HDOP,VDOP[,id_sistema]
    fix_.fix_mode = to_int(t[2], 0);
    fix_.pdop = to_double(t[15]);
    fix_.hdop = to_double(t[16]);
    fix_.vdop = to_double(t[17]);

    System system = t.size() >= 19 ? system_for_id(t[18]) : System::Unknown;
    if (system == System::Unknown) {
        system = system_for_talker(t[0].substr(1, 2));
    }

    // Con la constelación conocida, la lista sustituye a la anterior de esa constelación
    // (aunque venga vacía: ya no se usa ninguno). Con GN sin id, se reparte por número.
    std::map<System, std::set<int>> lists;
    if (system != System::Unknown) {
        lists[system];
    }
    if (system == System::Gps) {
        lists[System::Sbas]; // los SBAS (EGNOS) vienen en la lista de GPS
    }
    for (int i = 3; i <= 14; ++i) {
        if (t[i].empty()) {
            continue;
        }
        const int prn = to_int(t[i], 0);
        lists[resolve_system(system, prn)].insert(prn);
    }
    for (auto& [sys, prns] : lists) {
        UsedSet& used = used_[sys];
        used.prns = std::move(prns);
        used.epoch = epoch_;
    }
    rebuild_sky();
}

void Parser::rebuild_sky() {
    // Un mismo satélite sale en varios grupos si se recibe en varias señales (L1, L5…): se
    // junta en uno, con la mejor señal y la posición que se sepa.
    std::map<std::pair<System, int>, Satellite> merged;
    for (auto it = gsv_.begin(); it != gsv_.end();) {
        if (epoch_ - it->second.epoch > kStaleEpochs) {
            it = gsv_.erase(it);
            continue;
        }
        for (const Satellite& sat : it->second.complete) {
            auto [pos, inserted] = merged.try_emplace({sat.system, sat.prn}, sat);
            if (!inserted) {
                Satellite& have = pos->second;
                have.snr = std::max(have.snr, sat.snr);
                if (have.elevation < 0) {
                    have.elevation = sat.elevation;
                }
                if (have.azimuth < 0) {
                    have.azimuth = sat.azimuth;
                }
            }
        }
        ++it;
    }
    for (auto it = used_.begin(); it != used_.end();) {
        if (epoch_ - it->second.epoch > kStaleEpochs) {
            it = used_.erase(it);
        } else {
            ++it;
        }
    }

    fix_.sky.clear();
    fix_.sky.reserve(merged.size());
    for (auto& [key, sat] : merged) {
        const auto used = used_.find(sat.system);
        sat.used = used != used_.end() && used->second.prns.count(sat.prn) > 0;
        fix_.sky.push_back(sat);
    }
}

} // namespace gps

/*
 * SPDX-License-Identifier: MIT
 */

#include "track_recorder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace gps {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusM = 6371008.8;
constexpr double kJitterM = 2.0;

double rad(double deg) {
    return deg * kPi / 180.0;
}

} // namespace

double distance_m(double lat1, double lon1, double lat2, double lon2) {
    const double dlat = rad(lat2 - lat1);
    const double dlon = rad(lon2 - lon1);
    const double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
                     std::cos(rad(lat1)) * std::cos(rad(lat2)) * std::sin(dlon / 2) * std::sin(dlon / 2);
    return 2.0 * kEarthRadiusM * std::asin(std::min(1.0, std::sqrt(a)));
}

double track_length_m(const Track& track) {
    double total = 0.0;
    for (std::size_t i = 1; i < track.points.size(); ++i) {
        const auto& a = track.points[i - 1];
        const auto& b = track.points[i];
        total += distance_m(a.lat, a.lon, b.lat, b.lon);
    }
    return total;
}

std::string track_file_stem(const Fix& fix) {
    if (fix.year == 0 || fix.month == 0 || fix.day == 0) {
        return "";
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d_%02d%02d%02d", fix.year, fix.month, fix.day,
                  fix.hour, fix.minute, static_cast<int>(fix.second));
    return buf;
}

std::string format_duration(double seconds) {
    const long total = std::max(0L, static_cast<long>(seconds));
    const long h = total / 3600;
    const long m = (total / 60) % 60;
    const long s = total % 60;
    char buf[32];
    if (h > 0) {
        std::snprintf(buf, sizeof(buf), "%ld:%02ld:%02ld", h, m, s);
    } else {
        std::snprintf(buf, sizeof(buf), "%ld:%02ld", m, s);
    }
    return buf;
}

std::string format_distance(double meters) {
    char buf[32];
    if (meters < 1000.0) {
        std::snprintf(buf, sizeof(buf), "%.0f m", std::max(0.0, meters));
        return buf;
    }
    std::snprintf(buf, sizeof(buf), "%.1f km", meters / 1000.0);
    std::string out = buf;
    std::replace(out.begin(), out.end(), '.', ',');
    return out;
}

TrackRecorder::TrackRecorder(double min_distance_m, double max_interval_s)
    : min_distance_m_(min_distance_m), max_interval_s_(max_interval_s) {}

void TrackRecorder::reset(std::string name) {
    track_ = Track{};
    track_.name = std::move(name);
    length_m_ = 0.0;
    last_t_ = 0.0;
}

bool TrackRecorder::offer(const Fix& fix, double t_sec) {
    if (!fix.valid) {
        return false;
    }
    TrackPoint point;
    point.lat = fix.lat;
    point.lon = fix.lon;
    point.ele_m = fix.alt_m;
    point.has_ele = fix.fix_quality > 0;
    point.time = fix.iso_time();

    if (track_.points.empty()) {
        track_.points.push_back(point);
        last_t_ = t_sec;
        return true;
    }

    const auto& last = track_.points.back();
    const double d = distance_m(last.lat, last.lon, point.lat, point.lon);
    const bool moved = d >= min_distance_m_;
    const bool slow_but_moving = d > kJitterM && (t_sec - last_t_) >= max_interval_s_;
    if (!moved && !slow_but_moving) {
        return false;
    }
    track_.points.push_back(point);
    length_m_ += d;
    last_t_ = t_sec;
    return true;
}

} // namespace gps

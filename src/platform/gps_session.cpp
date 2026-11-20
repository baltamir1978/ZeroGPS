/*
 * SPDX-License-Identifier: MIT
 */

#include "gps_session.h"

#include "storage.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace platform {
namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

// El GPX se reescribe entero cada minuto mientras se graba: si se acaba la batería o la app
// se cierra mal, se pierde como mucho un minuto. Más a menudo sería escribir en la tarjeta
// sin necesidad (y gastar).
constexpr auto kSaveEvery = std::chrono::seconds(60);

std::string settings_path() {
    fs::path root;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        root = xdg;
    } else if (const char* home = std::getenv("HOME")) {
        root = fs::path(home) / ".config";
    } else {
        root = fs::current_path();
    }
    return (root / "zerogps" / "maps.conf").string();
}

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) {
        return "";
    }
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string system_time_stem() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&now, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H%M%S", &tm);
    return buf;
}

// La posición se reescribe en maps.conf al alejarse esto de la última guardada (o cada
// rato si se mueve algo): si se acaba la batería, el mapa vuelve a abrirse cerca de donde
// estabas.
constexpr double kSaveDistanceM = 200.0;
constexpr double kSaveMinMoveM = 20.0;
constexpr auto kSavePositionEvery = std::chrono::minutes(5);

// Escribe en un temporal y lo renombra: un corte a mitad no deja un GPX roto.
void write_gpx_atomic(const std::string& path, const gps::Track& track) {
    const std::string tmp = path + ".tmp";
    std::string error;
    if (gps::save_gpx_file(tmp, track, error)) {
        std::error_code ec;
        fs::rename(tmp, path, ec);
    }
}

} // namespace

GpsSession::GpsSession() {
    load_settings();
    gnss_.set_on_fix([this](const gps::Fix& fix) { on_fix(fix); });
}

GpsSession::~GpsSession() {
    if (recording()) {
        stop_recording();
    }
    save_settings(); // la vista y la última posición, para abrir ahí la próxima vez
    // El hilo del GNSS llama a on_fix(): tiene que haber acabado antes de que se destruyan
    // el mutex y el grabador.
    gnss_.stop_and_join();
}

void GpsSession::acquire() {
    if (users_++ == 0) {
        gnss_.start();
    }
}

void GpsSession::release() {
    if (users_ > 0 && --users_ == 0) {
        gnss_.stop();
    }
}

gps::Fix GpsSession::fix() const {
    return gnss_.fix();
}

bool GpsSession::recording() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return recording_;
}

void GpsSession::start_recording() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (recording_) {
            return;
        }
        const gps::Fix now = gnss_.fix();
        std::string stem = gps::track_file_stem(now);
        if (stem.empty()) {
            stem = system_time_stem();
        }
        recorder_.reset(stem);
        recording_path_ = (fs::path(tracks_dir()) / (stem + ".gpx")).string();
        recording_start_ = Clock::now();
        last_save_ = recording_start_;
        saved_points_ = 0;
        recording_ = true;
        ++revision_;
    }
    acquire();
}

std::string GpsSession::stop_recording() {
    std::string path;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!recording_) {
            return "";
        }
        recording_ = false;
        if (!recorder_.track().points.empty()) {
            write_gpx_atomic(recording_path_, recorder_.track());
            path = recording_path_;
        }
        ++revision_;
    }
    release();
    return path;
}

GpsSession::RecordingStatus GpsSession::recording_status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    RecordingStatus status;
    status.active = recording_;
    status.points = recorder_.track().points.size();
    status.length_m = recorder_.length_m();
    status.path = recording_path_;
    if (recording_) {
        status.elapsed_s = std::chrono::duration<double>(Clock::now() - recording_start_).count();
    }
    return status;
}

std::uint64_t GpsSession::track_revision() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return revision_;
}

void GpsSession::copy_recorded_points(std::vector<std::pair<double, double>>& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    out.clear();
    if (!recording_) {
        return;
    }
    out.reserve(recorder_.track().points.size());
    for (const auto& p : recorder_.track().points) {
        out.emplace_back(p.lat, p.lon);
    }
}

void GpsSession::on_fix(const gps::Fix& fix) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!recording_) {
        return;
    }
    const double t = std::chrono::duration<double>(Clock::now() - recording_start_).count();
    if (recorder_.offer(fix, t)) {
        ++revision_;
    }
    if (Clock::now() - last_save_ >= kSaveEvery && recorder_.track().points.size() != saved_points_) {
        save_recording_locked();
    }
}

void GpsSession::save_recording_locked() {
    write_gpx_atomic(recording_path_, recorder_.track());
    saved_points_ = recorder_.track().points.size();
    last_save_ = Clock::now();
}

void GpsSession::set_overlay(gps::Track track) {
    overlay_ = std::move(track);
}

void GpsSession::clear_overlay() {
    overlay_ = gps::Track{};
}

void GpsSession::set_tiles_dir(const std::string& dir) {
    if (dir == tiles_dir_) {
        return;
    }
    tiles_dir_ = dir;
    save_settings();
}

void GpsSession::remember(const gps::Fix& fix) {
    if (!fix.valid) {
        return;
    }
    last_position_ = {true, fix.lat, fix.lon};
    const double moved = saved_position_.valid
                             ? gps::distance_m(saved_position_.lat, saved_position_.lon, fix.lat, fix.lon)
                             : kSaveDistanceM;
    const bool due = Clock::now() - saved_at_ >= kSavePositionEvery && moved >= kSaveMinMoveM;
    if (moved >= kSaveDistanceM || due) {
        save_settings();
        mark_saved();
    }
}

void GpsSession::mark_saved() {
    saved_position_ = last_position_;
    saved_at_ = Clock::now();
}

void GpsSession::set_online(bool on) {
    if (on == online_) {
        return;
    }
    online_ = on;
    save_settings();
}

void GpsSession::set_layer(map::MapLayer layer) {
    if (layer == layer_) {
        return;
    }
    layer_ = layer;
    save_settings();
}

std::vector<std::string> GpsSession::tile_roots() const {
    std::vector<std::string> roots{tiles_dir_};
    for (const auto& option : storage_options()) {
        if (option.tiles_dir != tiles_dir_) {
            roots.push_back(option.tiles_dir);
        }
    }
    if (!bundled_tiles_dir_.empty()) {
        roots.push_back(bundled_tiles_dir_);
    }
    return roots;
}

void GpsSession::load_settings() {
    tiles_dir_ = default_tiles_dir();
    online_ = false; // por defecto sin wifi: la batería es pequeña
    std::ifstream in(settings_path());
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos || trim(line).rfind('#', 0) == 0) {
            continue;
        }
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        if (key == "tiles_dir" && !value.empty()) {
            tiles_dir_ = value;
        } else if (key == "layer") {
            map::layer_from_key(value, layer_);
        } else if (key == "online") {
            online_ = (value == "yes" || value == "true" || value == "1");
        } else if (key == "view" || key == "last") {
            // "lat,lon[,zoom]"
            double lat = 0.0;
            double lon = 0.0;
            int zoom = 0;
            const int n = std::sscanf(value.c_str(), "%lf,%lf,%d", &lat, &lon, &zoom);
            if (n < 2 || lat < -85.0 || lat > 85.0 || lon < -180.0 || lon > 180.0) {
                continue;
            }
            if (key == "view") {
                map_view_.lat = lat;
                map_view_.lon = lon;
                if (n == 3 && zoom >= 5 && zoom <= 19) {
                    map_view_.zoom = zoom;
                }
            } else {
                last_position_ = {true, lat, lon};
            }
        }
    }
    mark_saved();
}

void GpsSession::save_settings() const {
    const fs::path path(settings_path());
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return;
    }
    out << "# Ajustes de mapas de ZeroGPS (los cambia la app)\n";
    out << "tiles_dir=" << tiles_dir_ << "\n";
    out << "online=" << (online_ ? "yes" : "no") << "\n";
    out << "layer=" << map::layer_info(layer_).key << "\n";
    char buf[96];
    std::snprintf(buf, sizeof(buf), "view=%.6f,%.6f,%d\n", map_view_.lat, map_view_.lon, map_view_.zoom);
    out << buf;
    if (last_position_.valid) {
        std::snprintf(buf, sizeof(buf), "last=%.6f,%.6f\n", last_position_.lat, last_position_.lon);
        out << buf;
    }
}

} // namespace platform

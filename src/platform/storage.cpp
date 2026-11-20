/*
 * SPDX-License-Identifier: MIT
 */

#include "storage.h"

#include "storage_mounts.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include <sys/statvfs.h>

namespace platform {
namespace {

namespace fs = std::filesystem;

fs::path home_dir() {
    if (const char* home = std::getenv("HOME")) {
        return fs::path(home);
    }
    return fs::current_path();
}

fs::path data_root() {
    if (const char* xdg = std::getenv("XDG_DATA_HOME")) {
        if (*xdg) {
            return fs::path(xdg);
        }
    }
    return home_dir() / ".local" / "share";
}

} // namespace

std::string default_tiles_dir() {
    return (data_root() / "zerogps" / "tiles").string();
}

std::string tracks_dir() {
    const fs::path dir = home_dir() / "Tracks";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir.string();
}

std::vector<StorageOption> storage_options() {
    std::vector<StorageOption> out;
    out.push_back({"Interna (SD del sistema)", default_tiles_dir()});

    std::ifstream in("/proc/mounts");
    std::stringstream text;
    text << in.rdbuf();
    for (const auto& mount : storage::removable_mounts(text.str())) {
        const fs::path p(mount);
        out.push_back({p.filename().string(), (p / "ZeroGPS" / "tiles").string()});
    }
    return out;
}

long long free_bytes(const std::string& path) {
    fs::path p(path);
    std::error_code ec;
    while (!p.empty() && !fs::exists(p, ec)) {
        p = p.parent_path();
    }
    if (p.empty()) {
        return -1;
    }
    struct statvfs st {};
    if (statvfs(p.c_str(), &st) != 0) {
        return -1;
    }
    return static_cast<long long>(st.f_bavail) * static_cast<long long>(st.f_frsize);
}

std::vector<TrackFile> list_tracks() {
    std::vector<TrackFile> out;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(tracks_dir(), ec)) {
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".gpx") {
            continue;
        }
        TrackFile file;
        file.name = entry.path().stem().string();
        file.path = entry.path().string();
        file.size = entry.file_size(ec);
        out.push_back(std::move(file));
    }
    std::sort(out.begin(), out.end(),
              [](const TrackFile& a, const TrackFile& b) { return a.name > b.name; });
    return out;
}

bool delete_file(const std::string& path) {
    std::error_code ec;
    return fs::remove(path, ec);
}

} // namespace platform

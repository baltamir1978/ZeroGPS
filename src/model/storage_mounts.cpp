/*
 * SPDX-License-Identifier: MIT
 */

#include "storage_mounts.h"

#include <algorithm>
#include <array>
#include <sstream>

namespace storage {
namespace {

std::string unescape(const std::string& field) {
    std::string out;
    for (std::size_t i = 0; i < field.size(); ++i) {
        if (field[i] == '\\' && i + 3 < field.size()) {
            const std::string oct = field.substr(i + 1, 3);
            if (std::all_of(oct.begin(), oct.end(), [](char c) { return c >= '0' && c <= '7'; })) {
                out.push_back(static_cast<char>(std::stoi(oct, nullptr, 8)));
                i += 3;
                continue;
            }
        }
        out.push_back(field[i]);
    }
    return out;
}

bool starts_with(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

} // namespace

std::vector<std::string> removable_mounts(const std::string& proc_mounts) {
    static const std::array<const char*, 8> kDataFs = {"vfat", "exfat", "ext4", "ext3",
                                                       "ntfs", "ntfs3", "fuseblk", "f2fs"};
    std::vector<std::string> out;
    std::istringstream in(proc_mounts);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        std::string device, mount_point, fs;
        if (!(fields >> device >> mount_point >> fs)) {
            continue;
        }
        mount_point = unescape(mount_point);
        const bool under_media = starts_with(mount_point, "/media/") ||
                                 starts_with(mount_point, "/mnt/") ||
                                 starts_with(mount_point, "/run/media/");
        const bool data_fs = std::find_if(kDataFs.begin(), kDataFs.end(),
                                          [&fs](const char* f) { return fs == f; }) != kDataFs.end();
        if (under_media && data_fs &&
            std::find(out.begin(), out.end(), mount_point) == out.end()) {
            out.push_back(mount_point);
        }
    }
    return out;
}

} // namespace storage

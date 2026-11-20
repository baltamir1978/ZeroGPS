/*
 * SPDX-License-Identifier: MIT
 */

#include "tile_provider.h"

#include <cstdio>
#include <csetjmp>
#include <filesystem>
#include <system_error>

#include <jpeglib.h>

namespace platform {
namespace {

namespace fs = std::filesystem;

// Manejo de errores de libjpeg sin que el proceso haga exit() con un JPEG corrupto.
struct JpegError {
    jpeg_error_mgr base;
    std::jmp_buf jump;
};

void jpeg_on_error(j_common_ptr info) {
    auto* err = reinterpret_cast<JpegError*>(info->err);
    std::longjmp(err->jump, 1);
}

inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

bool decode_jpeg(const std::string& path, DecodedTile& out) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        return false;
    }

    jpeg_decompress_struct cinfo;
    JpegError err;
    cinfo.err = jpeg_std_error(&err.base);
    err.base.error_exit = jpeg_on_error;
    if (setjmp(err.jump)) {
        jpeg_destroy_decompress(&cinfo);
        std::fclose(file);
        return false;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, file);
    jpeg_read_header(&cinfo, TRUE);
    jpeg_start_decompress(&cinfo);

    const int w = static_cast<int>(cinfo.output_width);
    const int h = static_cast<int>(cinfo.output_height);
    const int comps = cinfo.output_components;
    out.w = w;
    out.h = h;
    out.pixels.assign(static_cast<std::size_t>(w) * h, 0);

    std::vector<uint8_t> row(static_cast<std::size_t>(w) * comps);
    uint8_t* row_ptr = row.data();
    while (static_cast<int>(cinfo.output_scanline) < h) {
        const int y = static_cast<int>(cinfo.output_scanline);
        jpeg_read_scanlines(&cinfo, &row_ptr, 1);
        uint16_t* dst = &out.pixels[static_cast<std::size_t>(y) * w];
        for (int x = 0; x < w; ++x) {
            if (comps >= 3) {
                dst[x] = rgb565(row[x * comps], row[x * comps + 1], row[x * comps + 2]);
            } else {
                const uint8_t g = row[x * comps];
                dst[x] = rgb565(g, g, g);
            }
        }
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    std::fclose(file);
    return out.ok();
}

} // namespace

TileProvider::TileProvider(std::vector<std::string> roots) : roots_(std::move(roots)) {}

void TileProvider::set_roots(std::vector<std::string> roots) {
    roots_ = std::move(roots);
    missing_.clear();
}

void TileProvider::invalidate_missing() {
    missing_.clear();
}

std::string TileProvider::find_file(const map::Tile& tile) const {
    const std::string rel = map::tile_cache_path(tile); // "z/x/y.jpeg"
    std::error_code ec;
    for (const auto& root : roots_) {
        const fs::path candidate = fs::path(root) / rel;
        if (fs::is_regular_file(candidate, ec)) {
            return candidate.string();
        }
    }
    return "";
}

const DecodedTile* TileProvider::get(const map::Tile& tile) {
    const std::string key = map::tile_cache_path(tile);

    const auto it = cache_.find(key);
    if (it != cache_.end()) {
        order_.remove(key);
        order_.push_front(key);
        return &it->second;
    }
    if (missing_.count(key)) {
        return nullptr;
    }

    const std::string path = find_file(tile);
    DecodedTile tile_data;
    if (path.empty() || !decode_jpeg(path, tile_data)) {
        missing_.insert(key);
        return nullptr;
    }

    // Se expulsa antes de insertar: así el puntero que se devuelve sigue valiendo hasta la
    // próxima llamada.
    while (order_.size() >= capacity_) {
        cache_.erase(order_.back());
        order_.pop_back();
    }
    auto& stored = cache_[key];
    stored = std::move(tile_data);
    order_.push_front(key);
    return &stored;
}

} // namespace platform

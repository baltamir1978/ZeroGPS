/*
 * SPDX-License-Identifier: MIT
 */

#include "map_layers.h"

#include <array>
#include <cstdio>

namespace map {
namespace {

// max_zoom: el MTN 1:25.000 no da más de sí a partir de z17; la ortofoto PNOA (25 cm/px)
// llega a z19; el Base sigue nítido hasta z18.
const std::array<LayerInfo, kLayerCount> kLayers = {{
    {MapLayer::Topo, "topo", "Topográfico", "mapa-raster", "MTN", "", 17, "© IGN"},
    {MapLayer::Ortho, "pnoa", "Satélite", "pnoa-ma", "OI.OrthoimageCoverage", "pnoa", 19, "PNOA © IGN"},
    {MapLayer::Base, "base", "IGN Base", "ign-base", "IGNBaseTodo", "base", 18, "© IGN"},
}};

} // namespace

const LayerInfo& layer_info(MapLayer layer) {
    const int i = static_cast<int>(layer);
    return kLayers[static_cast<std::size_t>(i >= 0 && i < kLayerCount ? i : 0)];
}

MapLayer next_layer(MapLayer layer) {
    return static_cast<MapLayer>((static_cast<int>(layer) + 1) % kLayerCount);
}

bool layer_from_key(const std::string& key, MapLayer& layer) {
    for (const auto& info : kLayers) {
        if (key == info.key) {
            layer = info.id;
            return true;
        }
    }
    return false;
}

std::string tile_url(MapLayer layer, const Tile& tile) {
    const LayerInfo& info = layer_info(layer);
    char buf[384];
    std::snprintf(buf, sizeof(buf),
                  "https://www.ign.es/wmts/%s?service=WMTS&request=GetTile&version=1.0.0"
                  "&layer=%s&style=default&format=image/jpeg&tilematrixset=GoogleMapsCompatible"
                  "&tilematrix=%d&tilerow=%d&tilecol=%d",
                  info.service, info.wmts_layer, tile.z, tile.y, tile.x);
    return buf;
}

std::string tile_path(MapLayer layer, const Tile& tile) {
    const LayerInfo& info = layer_info(layer);
    const std::string rel = tile_cache_path(tile);
    return info.subdir[0] ? std::string(info.subdir) + "/" + rel : rel;
}

long long estimated_tile_bytes(MapLayer layer, int z) {
    constexpr long long kKB = 1024;
    switch (layer) {
        case MapLayer::Ortho: // medido: 10 KB a z12, 15-17 KB de z14 a z18
            return (z <= 12 ? 12 : 19) * kKB;
        case MapLayer::Base: // medido: 9,5 KB a z12-14, 5,7 a z16, 3,2 a z18
            return (z <= 14 ? 11 : (z <= 16 ? 7 : 4)) * kKB;
        case MapLayer::Topo: // medido: 25 KB a z12, 21-23 a z13-14, 19 a z16
        default:
            return (z <= 12 ? 28 : (z <= 15 ? 26 : (z == 16 ? 22 : 14))) * kKB;
    }
}

} // namespace map

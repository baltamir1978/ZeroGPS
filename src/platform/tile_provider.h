/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "map_layers.h"
#include "map_tiles.h"

#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace platform {

// Una tesela ya decodificada a RGB565, lista para copiar al canvas.
struct DecodedTile {
    int w = 0;
    int h = 0;
    std::vector<uint16_t> pixels; // RGB565, w*h

    bool ok() const {
        return w > 0 && h > 0 && !pixels.empty();
    }
};

/**
 * Da teselas decodificadas a partir de los .jpeg, buscándolas en orden en `roots` (la carpeta
 * de descargas elegida, las otras tarjetas y las que vienen con la app). Guarda en memoria
 * las últimas (LRU, ~8 MB) para no decodificar lo mismo en cada refresco, y recuerda las que
 * no están para no mirar el disco una y otra vez (hasta `invalidate_missing`).
 */
class TileProvider {
public:
    explicit TileProvider(std::vector<std::string> roots);

    // nullptr si la tesela no está en ninguna carpeta (o el JPEG está roto).
    const DecodedTile* get(const map::Tile& tile);

    // Tras descargar teselas: que se vuelvan a buscar las que faltaban.
    void invalidate_missing();

    void set_roots(std::vector<std::string> roots);
    // Cambia de mapa (topográfico, satélite...): se vacía la memoria.
    void set_layer(map::MapLayer layer);
    map::MapLayer layer() const {
        return layer_;
    }

private:
    std::string find_file(const map::Tile& tile) const;

    std::vector<std::string> roots_;
    map::MapLayer layer_ = map::MapLayer::Topo;
    std::unordered_map<std::string, DecodedTile> cache_;
    std::list<std::string> order_; // claves por antigüedad de uso (frente = reciente)
    std::unordered_set<std::string> missing_;
    std::size_t capacity_ = 64;
};

} // namespace platform

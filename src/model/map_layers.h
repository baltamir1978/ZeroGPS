/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "map_tiles.h"

#include <string>

namespace map {

// Los mapas del IGN que la app sabe pintar: todos por WMTS, con el conjunto
// GoogleMapsCompatible y en JPEG (lo único que decodifica la app).
enum class MapLayer {
    Topo = 0,  // MTN: el topográfico de siempre (curvas de nivel, caminos)
    Ortho = 1, // PNOA: ortofoto aérea (la «vista satélite»)
    Base = 2,  // IGN Base: cartografía simplificada, tipo callejero
};
constexpr int kLayerCount = 3;

struct LayerInfo {
    MapLayer id;
    const char* key;         // en maps.conf
    const char* name;        // en pantalla
    const char* service;     // https://www.ign.es/wmts/<service>
    const char* wmts_layer;  // identificador de capa en el GetCapabilities
    const char* subdir;      // subcarpeta en la caché ("" = la raíz, por compatibilidad)
    int max_zoom;            // por encima no hay más detalle: se amplía el último
    const char* attribution; // texto corto para la esquina del mapa
};

const LayerInfo& layer_info(MapLayer layer);
MapLayer next_layer(MapLayer layer);
// false si la clave no es ninguna capa (se deja `layer` como estaba).
bool layer_from_key(const std::string& key, MapLayer& layer);

// URL GetTile del IGN para esa capa.
std::string tile_url(MapLayer layer, const Tile& tile);
// Ruta relativa en la caché: "z/x/y.jpeg" para el topográfico (así siguen valiendo las
// teselas ya bajadas y las de la app) y "<subdir>/z/x/y.jpeg" para las demás.
std::string tile_path(MapLayer layer, const Tile& tile);

// Bytes por tesela de media, medidos en Guadarrama el 20-nov-2026 con un ~15 % de margen.
// La ortofoto pesa poco más o menos igual a todos los zooms; el Base cada vez menos (más
// zonas lisas); el topográfico es el más pesado.
long long estimated_tile_bytes(MapLayer layer, int z);

} // namespace map

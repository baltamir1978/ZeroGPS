/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "map_layers.h"
#include "map_tiles.h"

#include <array>
#include <string>
#include <vector>

namespace map {

// Rectángulo geográfico (grados).
struct BBox {
    double min_lat = 0.0;
    double min_lon = 0.0;
    double max_lat = 0.0;
    double max_lon = 0.0;
};

// Zonas «completas» que se ofrecen ya hechas en la pantalla de descarga (montaña, sobre todo,
// que es donde no hay cobertura). Los límites son aproximados, con algo de margen.
struct ZonePreset {
    const char* name;
    BBox box;
};
const std::vector<ZonePreset>& zone_presets();

// Zooms que se pueden descargar. El tope real lo pone cada capa (`LayerInfo::max_zoom`: por
// encima no hay más detalle y las teselas se multiplican por cuatro en cada nivel); esto es
// sólo el mayor de todos.
constexpr int kDownloadZoomMin = 5;
constexpr int kDownloadZoomMax = 19;

// Tope por descarga: el IGN es un servicio público y la licencia no ampara volcarlo entero.
// 100 000 teselas son ~2 GB, de sobra para una sierra completa a zoom 16.
constexpr long long kMaxTilesPerDownload = 100000;

// Cuadrado de `radius_km` de semilado alrededor de un punto.
BBox bbox_around(double lat, double lon, double radius_km);

// Número de teselas de una zona a un zoom (sin generarlas).
long long count_tiles(const BBox& box, int z);

struct ZonePlan {
    int zmin = 0;
    int zmax = 0;
    long long tiles = 0;
    long long bytes = 0; // estimación
    std::array<long long, kDownloadZoomMax + 1> per_zoom{};
};

// Los zooms se recortan a [kDownloadZoomMin, max_zoom de la capa].
ZonePlan plan_zone(const BBox& box, int zmin, int zmax, MapLayer layer = MapLayer::Topo);

// Todas las teselas de la zona, de zoom menor a mayor (así, si se cancela, lo que queda
// descargado ya sirve para ver la zona entera aunque sea con menos detalle).
std::vector<Tile> zone_tiles(const BBox& box, int zmin, int zmax, MapLayer layer = MapLayer::Topo);

// El zoom mayor en el que la zona cabe entera en una ventana de w×h píxeles.
int fit_zoom(const BBox& box, int w, int h, int zmin, int zmax);

// "850 KB", "12,3 MB", "1,4 GB" (coma decimal, como se escribe en España).
std::string format_bytes(long long bytes);
// "12.345" (punto de millares).
std::string format_count(long long value);

} // namespace map

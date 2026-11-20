/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

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

// Zooms que se pueden descargar. Por encima de 17 el MTN del IGN no da más detalle (sólo
// amplía) y las teselas se multiplican por cuatro en cada nivel.
constexpr int kDownloadZoomMin = 5;
constexpr int kDownloadZoomMax = 17;

// Tope por descarga: el IGN es un servicio público y la licencia no ampara volcarlo entero.
// 100 000 teselas son ~2 GB, de sobra para una sierra completa a zoom 16.
constexpr long long kMaxTilesPerDownload = 100000;

// Cuadrado de `radius_km` de semilado alrededor de un punto.
BBox bbox_around(double lat, double lon, double radius_km);

// Número de teselas de una zona a un zoom (sin generarlas).
long long count_tiles(const BBox& box, int z);

// Bytes que ocupa de media una tesela del MTN (JPEG 256 px), medido con las de Guadarrama:
// unos 26 KB a zoom 12 y 21 KB a 13-14. En zonas de ciudad pesan más; se redondea hacia arriba.
long long estimated_tile_bytes(int z);

struct ZonePlan {
    int zmin = 0;
    int zmax = 0;
    long long tiles = 0;
    long long bytes = 0; // estimación
    std::array<long long, kDownloadZoomMax + 1> per_zoom{};
};

ZonePlan plan_zone(const BBox& box, int zmin, int zmax);

// Todas las teselas de la zona, de zoom menor a mayor (así, si se cancela, lo que queda
// descargado ya sirve para ver la zona entera aunque sea con menos detalle).
std::vector<Tile> zone_tiles(const BBox& box, int zmin, int zmax);

// El zoom mayor en el que la zona cabe entera en una ventana de w×h píxeles.
int fit_zoom(const BBox& box, int w, int h, int zmin, int zmax);

// "850 KB", "12,3 MB", "1,4 GB" (coma decimal, como se escribe en España).
std::string format_bytes(long long bytes);
// "12.345" (punto de millares).
std::string format_count(long long value);

} // namespace map

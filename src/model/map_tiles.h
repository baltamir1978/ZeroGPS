/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>
#include <vector>

namespace map {

// Una tesela del esquema «slippy map» / WMTS GoogleMapsCompatible (Web Mercator, 256 px).
struct Tile {
    int z = 0;
    int x = 0;
    int y = 0;
};

constexpr int kTileSize = 256;

// Conversión lon/lat <-> tesela (fraccional, para posicionar al píxel). z es el nivel de zoom.
double lon_to_tile_x(double lon_deg, int z);
double lat_to_tile_y(double lat_deg, int z);
double tile_x_to_lon(double x, int z);
double tile_y_to_lat(double y, int z);

// La tesela entera que contiene un punto.
Tile tile_at(double lat_deg, double lon_deg, int z);

// Teselas que cubren un rectángulo [lat/lon mín, lat/lon máx] a un zoom. Útil para descargar
// una zona para uso offline.
std::vector<Tile> tiles_for_bbox(double min_lat, double min_lon, double max_lat, double max_lon, int z);

// URL WMTS del IGN para la capa MTN (mapa topográfico, GoogleMapsCompatible, JPEG). El
// identificador de capa se confirmará con el GetCapabilities del servicio.
std::string ign_mtn_url(const Tile& tile);

// Ruta relativa en la caché local: "z/x/y.jpeg".
std::string tile_cache_path(const Tile& tile);

// Metros que mide un píxel del mapa a esa latitud y zoom.
double meters_per_pixel(double lat_deg, int z);

// Cada cuánto se refresca el mapa según el zoom: de cerca hay detalle y un paso se nota en
// pantalla; de lejos, aunque se recalcule cada segundo, el punto no se mueve ni un píxel y
// sólo se gasta batería. 1 s a z16-18, 2 s a z14-15, 5 s a z12-13 y 10 s por debajo.
unsigned map_refresh_ms(int z);

} // namespace map

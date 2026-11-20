/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace platform {

// $XDG_DATA_HOME (o ~/.local/share)/zerogps/tiles: el sitio por defecto de las teselas.
// En el Cardputer Zero el sistema va en la microSD, así que esto ya está en la tarjeta.
std::string default_tiles_dir();

// Dónde se guardan los GPX: ~/Tracks (a la vista, para copiarlos por scp o con la tarjeta).
std::string tracks_dir();

// Dónde se pueden guardar las teselas: la tarjeta del sistema y, si hay, cada tarjeta o
// pincho montado (en <montaje>/ZeroGPS/tiles).
struct StorageOption {
    std::string label;     // "Interna (SD)", "MI SD"...
    std::string tiles_dir; // carpeta de teselas en ese almacenamiento
};
std::vector<StorageOption> storage_options();

// Bytes libres para un usuario normal en el sistema de ficheros de `path` (o del primer
// directorio padre que exista). -1 si no se puede saber.
long long free_bytes(const std::string& path);

struct TrackFile {
    std::string name; // sin .gpx
    std::string path;
    std::uintmax_t size = 0;
};
// Los GPX de tracks_dir(), el más nuevo primero (el nombre empieza por la fecha).
std::vector<TrackFile> list_tracks();
bool delete_file(const std::string& path);

} // namespace platform

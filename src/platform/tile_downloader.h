/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "map_layers.h"
#include "map_tiles.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <sys/types.h>

namespace platform {

/**
 * Baja teselas del IGN a `dir/z/x/y.jpeg` en un hilo, saltándose las que ya están (así una
 * descarga cortada se retoma donde se quedó).
 *
 * Usa el `curl` del sistema en tandas de 48 teselas con `--parallel`: en una misma tanda se
 * reutilizan las conexiones HTTPS (menos negociaciones TLS, menos tiempo con la wifi a tope)
 * y no hace falta enlazar libcurl. Como mucho 3 peticiones a la vez, por respeto al servicio.
 * Cada tesela se escribe en `.part` y sólo se da por buena si es un JPEG; se para sola si se
 * queda sin espacio o sin red.
 */
class TileDownloader {
public:
    struct Progress {
        std::size_t total = 0;
        std::size_t done = 0;    // descargadas ahora
        std::size_t skipped = 0; // ya estaban
        std::size_t failed = 0;
        long long bytes = 0;     // bytes descargados ahora
        bool running = false;
        bool finished = false;
        bool cancelled = false;
        std::string error; // por qué se paró antes de acabar ("" si no)
    };

    TileDownloader() = default;
    ~TileDownloader();

    TileDownloader(const TileDownloader&) = delete;
    TileDownloader& operator=(const TileDownloader&) = delete;

    // `dir` es la carpeta raíz de teselas; cada capa va en su subcarpeta (map::tile_path).
    bool start(std::vector<map::Tile> tiles, std::string dir, map::MapLayer layer);
    void cancel();
    bool running() const;
    Progress progress() const;
    // Para la próxima descarga (tras leer el resultado).
    void reset();

private:
    void run();
    bool run_batch(const std::vector<map::Tile>& batch);

    std::vector<map::Tile> tiles_;
    std::string dir_;
    map::MapLayer layer_ = map::MapLayer::Topo;
    std::thread worker_;
    std::atomic<bool> cancel_{false};
    std::atomic<pid_t> child_{-1};

    mutable std::mutex mutex_;
    Progress progress_;
};

} // namespace platform

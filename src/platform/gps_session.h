/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "gnss_reader.h"
#include "gps_gpx.h"
#include "offline_zone.h"
#include "track_recorder.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace platform {

/**
 * Lo del GPS que es de la app entera y no de una pantalla: el GNSS, la grabación del track
 * (que sigue aunque salgas del mapa), el track cargado para verlo, dónde se quedó el mapa y
 * los ajustes de mapas (carpeta de teselas, modo online).
 *
 * El GNSS se enciende por demanda: cada pantalla que lo necesita llama a `acquire()` y luego
 * a `release()`, y la grabación cuenta como uno más. Con nadie usándolo se apaga el cap.
 *
 * Hilos: la grabación se alimenta desde el hilo del GNSS (por eso va con mutex); lo demás se
 * usa sólo desde el hilo de LVGL.
 */
class GpsSession {
public:
    GpsSession();
    ~GpsSession();

    GpsSession(const GpsSession&) = delete;
    GpsSession& operator=(const GpsSession&) = delete;

    // --- GNSS ---
    void acquire();
    void release();
    gps::Fix fix() const;
    bool simulated() const {
        return gnss_.is_simulated();
    }

    // --- Grabación ---
    bool recording() const;
    // Empieza un track nuevo. Si aún no hay hora del GNSS se nombra con la del sistema.
    void start_recording();
    // Para y guarda; devuelve la ruta del GPX ("" si no había puntos y no se guardó nada).
    std::string stop_recording();

    struct RecordingStatus {
        bool active = false;
        std::size_t points = 0;
        double length_m = 0.0;
        double elapsed_s = 0.0;
        std::string path;
    };
    RecordingStatus recording_status() const;
    // Cambia cada vez que se añade un punto: el mapa sólo copia el track si cambió.
    std::uint64_t track_revision() const;
    void copy_recorded_points(std::vector<std::pair<double, double>>& out) const;

    // --- Track cargado para verlo en el mapa ---
    void set_overlay(gps::Track track);
    void clear_overlay();
    const gps::Track& overlay() const {
        return overlay_;
    }

    // --- Vista del mapa (dónde se quedó; la usa la pantalla de descarga) ---
    struct MapView {
        double lat = 40.7930; // Navacerrada, hasta que haya una posición
        double lon = -4.0120;
        int zoom = 14;
        bool follow = true; // centrado en la posición del GNSS
    };
    MapView& map_view() {
        return map_view_;
    }
    const MapView& map_view() const {
        return map_view_;
    }

    // --- Última posición conocida (se guarda en maps.conf) ---
    struct LastPosition {
        bool valid = false;
        double lat = 0.0;
        double lon = 0.0;
    };
    const LastPosition& last_position() const {
        return last_position_;
    }
    // Desde el hilo de LVGL con cada fix (mapa, satélites): apunta la posición y la guarda de
    // vez en cuando (al moverse bastante), no a cada segundo, por la tarjeta y la batería.
    void remember(const gps::Fix& fix);
    // Guarda ya la vista del mapa y la última posición (al salir del mapa y de la app).
    void save_now() const {
        save_settings();
    }

    // --- Ajustes de mapas (se guardan en ~/.config/zerogps/maps.conf) ---
    const std::string& tiles_dir() const {
        return tiles_dir_;
    }
    void set_tiles_dir(const std::string& dir);
    bool online() const {
        return online_;
    }
    map::MapLayer layer() const {
        return layer_;
    }
    void set_layer(map::MapLayer layer);
    void set_online(bool on);
    // Carpetas donde buscar teselas, en orden: la elegida, las demás tarjetas, las de la app.
    std::vector<std::string> tile_roots() const;
    void set_bundled_tiles_dir(std::string dir) {
        bundled_tiles_dir_ = std::move(dir);
    }

private:
    void on_fix(const gps::Fix& fix);
    void save_recording_locked();
    void load_settings();
    void save_settings() const;
    void mark_saved();

    GnssReader gnss_;
    int users_ = 0;

    mutable std::mutex mutex_;
    bool recording_ = false;
    gps::TrackRecorder recorder_;
    std::string recording_path_;
    std::chrono::steady_clock::time_point recording_start_;
    std::chrono::steady_clock::time_point last_save_;
    std::uint64_t revision_ = 0;
    std::size_t saved_points_ = 0;

    gps::Track overlay_;
    MapView map_view_;
    LastPosition last_position_;
    LastPosition saved_position_; // la última que se escribió en maps.conf
    std::chrono::steady_clock::time_point saved_at_{};
    std::string tiles_dir_;
    std::string bundled_tiles_dir_;
    bool online_ = false;
    map::MapLayer layer_ = map::MapLayer::Topo;
};

} // namespace platform

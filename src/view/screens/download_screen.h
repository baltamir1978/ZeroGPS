/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "gps_session.h"
#include "offline_zone.h"
#include "storage.h"
#include "tile_downloader.h"

#include <array>
#include <string>
#include <vector>

namespace screen {

/**
 * Mapas offline: elegir el mapa (topográfico, satélite o IGN Base), una zona (alrededor de
 * donde está el mapa, o una zona completa ya hecha), los zooms y dónde guardarla; antes de bajar nada se ve cuántas teselas son, cuánto
 * ocuparán y cuánto sitio libre hay, y no deja empezar si no cabe o si pasa del tope.
 * Lo ya bajado se salta, así que repetir una descarga sólo baja lo que falta.
 */
class DownloadScreen : public BaseScreen {
public:
    DownloadScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~DownloadScreen() override;

protected:
    void build_content(lv_obj_t* content) override;

private:
    enum class State { Setup, Downloading, Result };
    static constexpr int kRows = 6;

    bool handle_key(uint32_t key);
    bool handle_back();
    void change(int delta);
    void start();
    void start_next_layer();
    void recompute();
    // Los mapas elegidos: uno, o los tres si `layer_choice_` es kLayerCount.
    std::vector<map::MapLayer> chosen_layers() const;
    std::string layers_name() const;
    // Zoom máximo efectivo de una capa (el elegido, recortado a lo que da esa capa).
    int zmax_for(map::MapLayer layer) const;
    // El mayor zoom útil entre los mapas elegidos.
    int top_zoom() const;
    void refresh();
    map::BBox current_box() const;
    std::string zone_name() const;

    static void refresh_observer(lv_observer_t* observer, lv_subject_t* subject);
    static void timer_cb(lv_timer_t* timer);

    platform::GpsSession& gps_;
    platform::TileDownloader downloader_;
    std::vector<platform::StorageOption> storages_;

    State state_ = State::Setup;
    int row_ = 1; // se empieza en «Zona»
    // 0..kLayerCount-1 = un mapa; kLayerCount = los tres (lo de por defecto: lo pidió Bruno).
    int layer_choice_ = map::kLayerCount;
    int zone_ = 0; // 0 = alrededor del mapa; 1.. = zone_presets()[zone_-1]
    int radius_index_ = 2; // 5 km: con los tres mapas y todo el zoom, ~53.000 teselas
    // Todo el zoom por defecto. zmax_ por encima del máximo de una capa = hasta su máximo.
    int zmin_ = map::kDownloadZoomMin;
    int zmax_ = map::kDownloadZoomMax;
    int storage_ = 0;

    // La descarga en curso: las capas se bajan una tras otra.
    std::vector<map::MapLayer> job_layers_;
    std::size_t job_index_ = 0;
    map::BBox job_box_;
    std::size_t job_total_ = 0;             // teselas de todas las capas
    platform::TileDownloader::Progress acc_; // lo de las capas ya terminadas
    map::MapLayer current_layer_ = map::MapLayer::Topo;

    map::ZonePlan plan_; // suma de los mapas elegidos
    long long free_ = -1;
    bool can_start_ = false;

    std::array<lv_obj_t*, kRows> row_boxes_{};
    std::array<lv_obj_t*, kRows> row_keys_{};
    std::array<lv_obj_t*, kRows> row_values_{};
    std::array<lv_obj_t*, 3> info_{};
    lv_obj_t* bar_{nullptr};
    lv_obj_t* hint_{nullptr};
    lv_timer_t* timer_{nullptr};
};

} // namespace screen

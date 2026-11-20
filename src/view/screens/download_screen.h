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
    void recompute();
    void refresh();
    map::BBox current_box() const;
    std::string zone_name() const;

    static void refresh_observer(lv_observer_t* observer, lv_subject_t* subject);
    static void timer_cb(lv_timer_t* timer);

    platform::GpsSession& gps_;
    platform::TileDownloader downloader_;
    std::vector<platform::StorageOption> storages_;

    State state_ = State::Setup;
    int row_ = 1; // se empieza en «Zona»; la capa sale de la que tenga el mapa
    map::MapLayer layer_ = map::MapLayer::Topo;
    int zone_ = 0; // 0 = alrededor del mapa; 1.. = zone_presets()[zone_-1]
    int radius_index_ = 3;
    int zmin_ = 8;
    int zmax_ = 15;
    int storage_ = 0;

    map::ZonePlan plan_;
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

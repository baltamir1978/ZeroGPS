/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "gps_nmea.h"
#include "gps_session.h"
#include "tile_downloader.h"
#include "tile_provider.h"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace screen {

/**
 * Mapa: teselas del IGN (de la tarjeta o, en modo online, bajadas al vuelo), tu posición, el
 * track que se está grabando y el track cargado desde Tracks.
 *
 * Batería: el refresco depende del zoom (`map::map_refresh_ms`: 1 s de cerca, hasta 10 s de
 * lejos) y sólo se repinta si algo cambió de verdad (te has movido al menos 2 px, zoom,
 * track, teselas nuevas). Las teselas se copian directamente al buffer del canvas.
 */
class MapScreen : public BaseScreen {
public:
    MapScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~MapScreen() override;

protected:
    void build_content(lv_obj_t* content) override;

private:
    bool handle_key(uint32_t key);
    void poll();
    void redraw(bool force);
    void draw_tiles();
    bool blit_tile(int tx, int ty, int dst_x, int dst_y);
    void fill_missing(int dst_x, int dst_y);
    void update_lines();
    void update_labels();
    void update_timer();
    void request_missing_tiles();
    void check_fetch_done();
    void pan(int dx_px, int dy_px);
    void set_zoom(int zoom);
    void apply_theme();

    // Pixel global (a este zoom) de una coordenada, y del centro de la vista.
    double gx(double lon) const;
    double gy(double lat) const;

    static void poll_timer_cb(lv_timer_t* timer);
    static void theme_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    platform::GpsSession& gps_;
    platform::TileProvider tiles_;
    platform::TileDownloader fetcher_;
    std::vector<map::Tile> fetch_request_;
    std::unordered_set<std::string> fetch_failed_;
    std::vector<map::Tile> missing_visible_;

    gps::Fix fix_;

    // Lo último que se pintó, para no repintar si no cambia nada.
    long long drawn_cx_ = 0;
    long long drawn_cy_ = 0;
    int drawn_zoom_ = -1;
    std::uint64_t drawn_revision_ = ~0ULL;
    bool drawn_fix_valid_ = false;
    long long drawn_fix_x_ = 0;
    long long drawn_fix_y_ = 0;

    std::vector<std::pair<double, double>> recorded_;
    std::vector<lv_point_precise_t> recorded_px_;
    std::vector<lv_point_precise_t> overlay_px_;

    lv_obj_t* canvas_ = nullptr;
    std::vector<uint8_t> canvas_buf_;
    lv_obj_t* overlay_line_ = nullptr;
    lv_obj_t* track_line_ = nullptr;
    lv_obj_t* marker_ = nullptr;
    lv_obj_t* status_ = nullptr;
    lv_obj_t* rec_ = nullptr;
    lv_obj_t* online_ = nullptr;
    lv_obj_t* attribution_ = nullptr;
    lv_timer_t* poll_timer_ = nullptr;
    uint32_t timer_period_ = 0;
};

} // namespace screen

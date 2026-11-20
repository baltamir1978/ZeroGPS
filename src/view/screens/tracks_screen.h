/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "gps_session.h"
#include "storage.h"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace screen {

/**
 * Tracks: la primera fila graba o para; debajo, los GPX de ~/Tracks (el más nuevo arriba).
 * ENTER en un track lo abre en el mapa; D lo borra (pidiendo confirmación); Q quita del mapa
 * el track cargado. Sólo hay temporizador mientras se graba (para el contador).
 */
class TracksScreen : public BaseScreen {
public:
    TracksScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~TracksScreen() override;

protected:
    void build_content(lv_obj_t* content) override;

private:
    static constexpr int kVisibleRows = 7;

    bool handle_key(uint32_t key);
    bool handle_back();
    void reload();
    void refresh();
    void toggle_recording();
    void open_selected();
    std::string info_for(const platform::TrackFile& file);
    void update_timer();

    static void refresh_observer(lv_observer_t* observer, lv_subject_t* subject);
    static void timer_cb(lv_timer_t* timer);

    platform::GpsSession& gps_;
    std::vector<platform::TrackFile> files_;
    std::unordered_map<std::string, std::string> info_cache_;
    int selected_ = 0; // 0 = fila de grabar
    int first_ = 0;    // primera fila visible
    int pending_delete_ = -1;
    std::string message_;

    struct Row {
        lv_obj_t* box = nullptr;
        lv_obj_t* name = nullptr;
        lv_obj_t* info = nullptr;
    };
    std::array<Row, kVisibleRows> rows_{};
    lv_obj_t* hint_{nullptr};
    lv_timer_t* timer_{nullptr};
};

} // namespace screen

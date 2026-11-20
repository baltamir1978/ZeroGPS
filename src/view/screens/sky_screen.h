/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "gps_nmea.h"
#include "gps_session.h"

#include <array>

namespace screen {

/**
 * Satélites: el cielo visto desde arriba (círculos de elevación, norte arriba) con cada
 * satélite en su sitio y el color de su constelación (relleno si se usa para la posición), y
 * al lado el estado del fix: coordenadas, altitud, precisión y DOP.
 *
 * Mantiene el GNSS encendido mientras está abierta. Se refresca una vez por segundo, que es
 * lo que tarda el GNSS en mandar una ráfaga nueva.
 */
class SkyScreen : public BaseScreen {
public:
    SkyScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~SkyScreen() override;

protected:
    void build_content(lv_obj_t* content) override;

private:
    static constexpr int kMaxDots = 48;
    static constexpr int kInfoLines = 7;
    static constexpr int kLegendSlots = 6;

    void refresh();
    void apply_theme();

    static void timer_cb(lv_timer_t* timer);
    static void theme_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    platform::GpsSession& gps_;
    gps::Fix fix_;

    std::array<lv_obj_t*, 3> rings_{};
    lv_obj_t* cross_h_ = nullptr;
    lv_obj_t* cross_v_ = nullptr;
    std::array<lv_point_precise_t, 2> cross_h_pts_{};
    std::array<lv_point_precise_t, 2> cross_v_pts_{};
    std::array<lv_obj_t*, 4> cardinals_{};
    std::array<lv_obj_t*, kMaxDots> dots_{};
    std::array<lv_obj_t*, kMaxDots> prns_{};
    lv_obj_t* unplaced_ = nullptr;

    std::array<lv_obj_t*, kInfoLines> info_{};
    std::array<lv_obj_t*, kLegendSlots> legend_dots_{};
    std::array<lv_obj_t*, kLegendSlots> legend_labels_{};

    lv_timer_t* timer_ = nullptr;
};

} // namespace screen

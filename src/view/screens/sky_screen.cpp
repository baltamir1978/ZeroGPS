/*
 * SPDX-License-Identifier: MIT
 */

#include "sky_screen.h"

#include "asset_manager.h"
#include "theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace screen {
namespace {

constexpr double kPi = 3.14159265358979323846;

// El cielo: un círculo de 64 px de radio a la izquierda (el contenido mide 320×140).
constexpr int32_t kCx = 70;
constexpr int32_t kCy = 70;
constexpr int32_t kR = 64;
constexpr int32_t kDot = 9;

// Columna de datos a la derecha.
constexpr int32_t kInfoX = 146;
constexpr int32_t kInfoW = view::kScreenWidth - kInfoX - 2;
constexpr int32_t kLineH = 16;
constexpr int32_t kLegendY = 112;
constexpr int32_t kLegendRowH = 14;
constexpr int32_t kLegendColW = 58;

// Precisión horizontal aproximada = HDOP × error típico de la distancia a cada satélite
// (unos 4 m para un receptor civil de una frecuencia). El GNSS no manda GST con el error de
// verdad, así que es una estimación y se muestra con «≈».
constexpr double kUereM = 4.0;

constexpr uint32_t kRefreshMs = 1000;

struct SystemStyle {
    gps::System system;
    const char* short_name;
    uint32_t color;
};

// Orden de la leyenda y colores (los mismos en tema claro y oscuro: se distinguen en ambos).
constexpr SystemStyle kSystems[] = {
    {gps::System::Gps, "GPS", 0x3B82F6},     {gps::System::Galileo, "GAL", 0xF59E0B},
    {gps::System::Glonass, "GLO", 0xEF4444}, {gps::System::Beidou, "BDS", 0x10B981},
    {gps::System::Qzss, "QZS", 0xA855F7},    {gps::System::Navic, "IRN", 0xEC4899},
    {gps::System::Sbas, "SBAS", 0x9CA3AF},   {gps::System::Unknown, "?", 0x9CA3AF},
};

const SystemStyle& style_for(gps::System system) {
    for (const auto& style : kSystems) {
        if (style.system == system) {
            return style;
        }
    }
    return kSystems[sizeof(kSystems) / sizeof(kSystems[0]) - 1];
}

void set_text(lv_obj_t* label, const char* text) {
    const char* current = lv_label_get_text(label);
    if (!current || std::strcmp(current, text) != 0) {
        lv_label_set_text(label, text);
    }
}

lv_obj_t* make_circle(lv_obj_t* parent, int32_t size) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, size, size);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t* make_label(lv_obj_t* parent, const lv_font_t* font) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font ? font : &lv_font_montserrat_12, 0);
    lv_label_set_text(label, "");
    return label;
}

} // namespace

SkyScreen::SkyScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets), gps_(view_model.gps()) {
    gps_.acquire();
    init();
    // Sin teclas propias: ESC vuelve al menú y T cambia el tema (lo hace el viewmodel).
    this->view_model().set_page_handlers(this, [](uint32_t) { return false; }, []() { return false; });
}

SkyScreen::~SkyScreen() {
    view_model().clear_page_handlers(this);
    if (timer_) {
        lv_timer_delete(timer_);
    }
    gps_.release();
}

void SkyScreen::build_content(lv_obj_t* content) {
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    const lv_font_t* tiny = assets().load_standard_font(9, app::StandardFontWeight::Regular);
    const lv_font_t* small = assets().load_standard_font(11, app::StandardFontWeight::Regular);
    const lv_font_t* bold = assets().load_standard_font(12);
    const lv_font_t* legend_font = assets().load_standard_font(10, app::StandardFontWeight::Regular);

    // Horizonte, 30° y 60° de elevación.
    for (std::size_t i = 0; i < rings_.size(); ++i) {
        const int32_t r = kR * static_cast<int32_t>(rings_.size() - i) / static_cast<int32_t>(rings_.size());
        rings_[i] = make_circle(content, 2 * r);
        lv_obj_set_pos(rings_[i], kCx - r, kCy - r);
        lv_obj_set_style_border_width(rings_[i], 1, 0);
        lv_obj_set_style_bg_opa(rings_[i], LV_OPA_TRANSP, 0);
    }
    cross_h_pts_ = {{{static_cast<float>(kCx - kR), static_cast<float>(kCy)},
                     {static_cast<float>(kCx + kR), static_cast<float>(kCy)}}};
    cross_v_pts_ = {{{static_cast<float>(kCx), static_cast<float>(kCy - kR)},
                     {static_cast<float>(kCx), static_cast<float>(kCy + kR)}}};
    cross_h_ = lv_line_create(content);
    lv_line_set_points(cross_h_, cross_h_pts_.data(), cross_h_pts_.size());
    cross_v_ = lv_line_create(content);
    lv_line_set_points(cross_v_, cross_v_pts_.data(), cross_v_pts_.size());

    // N arriba; Oeste es «O».
    const char* names[4] = {"N", "E", "S", "O"};
    const int32_t pos[4][2] = {{kCx + 3, kCy - kR + 1}, {kCx + kR - 9, kCy + 1},
                               {kCx + 3, kCy + kR - 13}, {kCx - kR + 3, kCy + 1}};
    for (int i = 0; i < 4; ++i) {
        cardinals_[i] = make_label(content, tiny);
        lv_label_set_text(cardinals_[i], names[i]);
        lv_obj_set_pos(cardinals_[i], pos[i][0], pos[i][1]);
    }

    for (int i = 0; i < kMaxDots; ++i) {
        dots_[i] = make_circle(content, kDot);
        lv_obj_set_style_border_width(dots_[i], 2, 0);
        lv_obj_add_flag(dots_[i], LV_OBJ_FLAG_HIDDEN);
        prns_[i] = make_label(content, tiny);
        lv_obj_add_flag(prns_[i], LV_OBJ_FLAG_HIDDEN);
    }
    unplaced_ = make_label(content, tiny);
    lv_obj_set_pos(unplaced_, 0, 128);

    for (int i = 0; i < kInfoLines; ++i) {
        info_[i] = make_label(content, i == 0 ? bold : small);
        lv_obj_set_width(info_[i], kInfoW);
        lv_label_set_long_mode(info_[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_pos(info_[i], kInfoX, i * kLineH);
    }
    for (int i = 0; i < kLegendSlots; ++i) {
        const int32_t x = kInfoX + (i % 3) * kLegendColW;
        const int32_t y = kLegendY + (i / 3) * kLegendRowH;
        legend_dots_[i] = make_circle(content, 7);
        lv_obj_set_pos(legend_dots_[i], x, y + 4);
        lv_obj_set_style_bg_opa(legend_dots_[i], LV_OPA_COVER, 0);
        legend_labels_[i] = make_label(content, legend_font);
        lv_obj_set_pos(legend_labels_[i], x + 10, y);
    }

    apply_theme();
    lv_subject_add_observer_obj(view_model().dark_mode_subject(), theme_observer_cb, content, this);
    timer_ = lv_timer_create(timer_cb, kRefreshMs, this);
    refresh();
}

void SkyScreen::refresh() {
    fix_ = gps_.fix();
    const auto colors = view::palette(view_model().is_dark_mode());

    // --- El cielo ---
    int dot = 0;
    int unplaced = 0;
    int used = 0;
    for (const auto& sat : fix_.sky) {
        if (sat.used) {
            ++used;
        }
        if (sat.elevation < 0 || sat.azimuth < 0) {
            ++unplaced; // recién encendido aún no sabe dónde está cada uno
            continue;
        }
        if (dot >= kMaxDots) {
            continue;
        }
        const double r = kR * (90.0 - std::min(sat.elevation, 90)) / 90.0;
        const double az = sat.azimuth * kPi / 180.0;
        const int32_t x = kCx + static_cast<int32_t>(std::lround(r * std::sin(az)));
        const int32_t y = kCy - static_cast<int32_t>(std::lround(r * std::cos(az)));
        const lv_color_t color = lv_color_hex(style_for(sat.system).color);

        lv_obj_t* d = dots_[dot];
        lv_obj_set_pos(d, x - kDot / 2, y - kDot / 2);
        lv_obj_set_style_border_color(d, color, 0);
        lv_obj_set_style_bg_color(d, sat.used ? color : colors.background, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        // Sin señal (lo sabe por el almanaque pero no lo oye): más tenue.
        lv_obj_set_style_opa(d, sat.snr < 0 ? LV_OPA_40 : LV_OPA_COVER, 0);
        lv_obj_remove_flag(d, LV_OBJ_FLAG_HIDDEN);

        char buf[8];
        std::snprintf(buf, sizeof(buf), "%d", sat.prn);
        set_text(prns_[dot], buf);
        lv_obj_set_pos(prns_[dot], x + kDot / 2 + 1, y - 6);
        lv_obj_set_style_text_color(prns_[dot], sat.snr < 0 ? colors.text_disabled : colors.text, 0);
        lv_obj_remove_flag(prns_[dot], LV_OBJ_FLAG_HIDDEN);
        ++dot;
    }
    for (int i = dot; i < kMaxDots; ++i) {
        lv_obj_add_flag(dots_[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(prns_[i], LV_OBJ_FLAG_HIDDEN);
    }
    char buf[64];
    if (unplaced > 0) {
        std::snprintf(buf, sizeof(buf), "+%d sin situar", unplaced);
        set_text(unplaced_, buf);
    } else {
        set_text(unplaced_, "");
    }

    // --- Datos ---
    const int mode = fix_.fix_mode > 0 ? fix_.fix_mode : (fix_.valid ? 3 : 1);
    const char* state = !fix_.valid || mode <= 1 ? "Sin posición" : (mode == 2 ? "Fix 2D" : "Fix 3D");
    std::snprintf(buf, sizeof(buf), "%s · %d/%zu sat", state, used > 0 ? used : fix_.satellites,
                  fix_.sky.size());
    set_text(info_[0], buf);

    if (fix_.valid) {
        std::snprintf(buf, sizeof(buf), "%.5f° %s", std::fabs(fix_.lat), fix_.lat >= 0 ? "N" : "S");
        set_text(info_[1], buf);
        std::snprintf(buf, sizeof(buf), "%.5f° %s", std::fabs(fix_.lon), fix_.lon >= 0 ? "E" : "O");
        set_text(info_[2], buf);
        if (mode == 2) {
            set_text(info_[3], "Alt —  (fix 2D)");
        } else {
            std::snprintf(buf, sizeof(buf), "Alt %.0f m · %.1f km/h", fix_.alt_m, fix_.speed_kmh);
            set_text(info_[3], buf);
        }
    } else {
        set_text(info_[1], "Lat —");
        set_text(info_[2], "Lon —");
        set_text(info_[3], "Alt —");
    }

    if (fix_.valid && fix_.hdop > 0.0) {
        std::snprintf(buf, sizeof(buf), "Precisión ≈ ±%.0f m", std::max(1.0, fix_.hdop * kUereM));
    } else {
        std::snprintf(buf, sizeof(buf), "Precisión —");
    }
    set_text(info_[4], buf);

    if (fix_.hdop > 0.0 && fix_.vdop > 0.0) {
        std::snprintf(buf, sizeof(buf), "HDOP %.1f · VDOP %.1f", fix_.hdop, fix_.vdop);
    } else if (fix_.hdop > 0.0) {
        std::snprintf(buf, sizeof(buf), "HDOP %.1f", fix_.hdop);
    } else {
        std::snprintf(buf, sizeof(buf), "HDOP —");
    }
    set_text(info_[5], buf);

    if (fix_.year != 0 || fix_.hour != 0 || fix_.minute != 0 || fix_.second != 0.0) {
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d UTC", fix_.hour, fix_.minute,
                      static_cast<int>(fix_.second));
    } else {
        std::snprintf(buf, sizeof(buf), "Hora —");
    }
    set_text(info_[6], buf);

    // --- Leyenda: usados/vistos por constelación (sólo las que se ven) ---
    int slot = 0;
    for (const auto& style : kSystems) {
        if (slot >= kLegendSlots) {
            break;
        }
        int seen = 0;
        int in_use = 0;
        for (const auto& sat : fix_.sky) {
            if (sat.system == style.system) {
                ++seen;
                in_use += sat.used ? 1 : 0;
            }
        }
        if (seen == 0) {
            continue;
        }
        std::snprintf(buf, sizeof(buf), "%s %d/%d", style.short_name, in_use, seen);
        set_text(legend_labels_[slot], buf);
        lv_obj_set_style_bg_color(legend_dots_[slot], lv_color_hex(style.color), 0);
        lv_obj_remove_flag(legend_dots_[slot], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(legend_labels_[slot], LV_OBJ_FLAG_HIDDEN);
        ++slot;
    }
    for (int i = slot; i < kLegendSlots; ++i) {
        lv_obj_add_flag(legend_dots_[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(legend_labels_[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void SkyScreen::apply_theme() {
    const auto colors = view::palette(view_model().is_dark_mode());
    for (auto* ring : rings_) {
        lv_obj_set_style_border_color(ring, colors.border, 0);
    }
    for (auto* line : {cross_h_, cross_v_}) {
        lv_obj_set_style_line_color(line, colors.border, 0);
        lv_obj_set_style_line_width(line, 1, 0);
    }
    for (auto* label : cardinals_) {
        lv_obj_set_style_text_color(label, colors.text_disabled, 0);
    }
    lv_obj_set_style_text_color(unplaced_, colors.text_disabled, 0);
    for (int i = 0; i < kInfoLines; ++i) {
        lv_obj_set_style_text_color(info_[i], i == 0 ? colors.primary : colors.text, 0);
    }
    for (auto* label : legend_labels_) {
        lv_obj_set_style_text_color(label, colors.text, 0);
    }
}

void SkyScreen::timer_cb(lv_timer_t* timer) {
    auto* screen = static_cast<SkyScreen*>(lv_timer_get_user_data(timer));
    if (screen) {
        screen->refresh();
    }
}

void SkyScreen::theme_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<SkyScreen*>(lv_observer_get_user_data(observer));
    if (screen && screen->unplaced_) {
        screen->apply_theme();
        screen->refresh();
    }
}

} // namespace screen

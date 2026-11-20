/*
 * SPDX-License-Identifier: MIT
 */

#include "tracks_screen.h"

#include "asset_manager.h"
#include "gps_gpx.h"
#include "offline_zone.h"
#include "theme.h"
#include "track_recorder.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <limits>

namespace screen {
namespace {

constexpr int32_t kMargin = 6;
constexpr int32_t kRowHeight = 17;
constexpr int32_t kTop = 2;
constexpr int32_t kW = view::kScreenWidth;
constexpr int32_t kH = view::kScreenHeight - view::kTitleBarHeight;

bool same_file(const std::string& a, const std::string& b) {
    std::error_code ec;
    return !a.empty() && !b.empty() && std::filesystem::equivalent(a, b, ec);
}

} // namespace

TracksScreen::TracksScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets), gps_(view_model.gps()) {
    reload();
    init();
    this->view_model().set_page_handlers(this, [this](uint32_t key) { return handle_key(key); },
                                         [this]() { return handle_back(); });
}

TracksScreen::~TracksScreen() {
    view_model().clear_page_handlers(this);
    if (timer_) {
        lv_timer_delete(timer_);
    }
}

void TracksScreen::build_content(lv_obj_t* content) {
    const lv_font_t* text_font = assets().load_standard_font(13, app::StandardFontWeight::Regular);
    const lv_font_t* info_font = assets().load_standard_font(11, app::StandardFontWeight::Regular);
    const lv_font_t* hint_font = assets().load_standard_font(11, app::StandardFontWeight::Regular);

    for (int i = 0; i < kVisibleRows; ++i) {
        Row& row = rows_[i];
        row.box = lv_obj_create(content);
        lv_obj_remove_style_all(row.box);
        lv_obj_set_size(row.box, kW - 2 * kMargin, kRowHeight);
        lv_obj_set_pos(row.box, kMargin, kTop + i * kRowHeight);
        lv_obj_set_style_radius(row.box, 3, 0);
        lv_obj_clear_flag(row.box, LV_OBJ_FLAG_SCROLLABLE);

        row.name = lv_label_create(row.box);
        lv_obj_set_style_text_font(row.name, text_font ? text_font : &lv_font_montserrat_14, 0);
        lv_label_set_long_mode(row.name, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(row.name, 175);
        lv_obj_align(row.name, LV_ALIGN_LEFT_MID, 5, 0);

        row.info = lv_label_create(row.box);
        lv_obj_set_style_text_font(row.info, info_font ? info_font : &lv_font_montserrat_12, 0);
        lv_obj_align(row.info, LV_ALIGN_RIGHT_MID, -5, 0);
    }

    hint_ = lv_label_create(content);
    lv_obj_set_style_text_font(hint_, hint_font ? hint_font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(hint_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(hint_, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(hint_, kW - 2 * kMargin);
    lv_obj_align(hint_, LV_ALIGN_BOTTOM_MID, 0, -2);

    lv_subject_add_observer_obj(view_model().dark_mode_subject(), refresh_observer, content, this);
    update_timer();
}

void TracksScreen::reload() {
    files_ = platform::list_tracks();
    const int count = static_cast<int>(files_.size()) + 1;
    selected_ = std::clamp(selected_, 0, count - 1);
    pending_delete_ = -1;
}

std::string TracksScreen::info_for(const platform::TrackFile& file) {
    const auto rec = gps_.recording_status();
    if (rec.active && same_file(rec.path, file.path)) {
        return "grabando";
    }
    const auto it = info_cache_.find(file.path);
    if (it != info_cache_.end()) {
        return it->second;
    }
    // Se lee sólo cuando la fila se ve, y se recuerda.
    gps::Track track;
    std::string error;
    std::string info;
    if (gps::load_gpx_file(file.path, track, error)) {
        info = gps::format_distance(gps::track_length_m(track)) + " · " +
               std::to_string(track.points.size()) + " pt";
    } else {
        info = "ilegible";
    }
    info_cache_[file.path] = info;
    return info;
}

void TracksScreen::refresh() {
    if (!hint_) {
        return;
    }
    const auto colors = view::palette(view_model().is_dark_mode());
    const int count = static_cast<int>(files_.size()) + 1;
    if (selected_ < first_) {
        first_ = selected_;
    } else if (selected_ >= first_ + kVisibleRows) {
        first_ = selected_ - kVisibleRows + 1;
    }

    const auto rec = gps_.recording_status();
    for (int i = 0; i < kVisibleRows; ++i) {
        Row& row = rows_[i];
        const int index = first_ + i;
        if (index >= count) {
            lv_obj_add_flag(row.box, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(row.box, LV_OBJ_FLAG_HIDDEN);
        const bool is_selected = index == selected_;
        lv_obj_set_style_bg_opa(row.box, is_selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(row.box, index == pending_delete_ ? lv_color_hex(0xC62828) : colors.primary, 0);
        const lv_color_t text = is_selected ? colors.background : colors.text;
        const lv_color_t dim = is_selected ? colors.background : colors.text_disabled;
        lv_obj_set_style_text_color(row.name, text, 0);
        lv_obj_set_style_text_color(row.info, dim, 0);

        if (index == 0) {
            if (rec.active) {
                lv_label_set_text(row.name, "■ Parar y guardar");
                std::string info = gps::format_distance(rec.length_m) + " · " +
                                   gps::format_duration(rec.elapsed_s);
                if (!gps_.fix().valid) {
                    info += " · sin señal";
                }
                lv_label_set_text(row.info, info.c_str());
            } else {
                lv_label_set_text(row.name, "● Grabar track nuevo");
                lv_label_set_text(row.info, files_.empty() ? "aún no hay tracks" : "");
            }
            if (!is_selected) {
                lv_obj_set_style_text_color(row.name, lv_color_hex(0xD32F2F), 0);
            }
            continue;
        }
        const auto& file = files_[static_cast<std::size_t>(index - 1)];
        lv_label_set_text(row.name, file.name.c_str());
        lv_label_set_text(row.info, info_for(file).c_str());
    }

    const char* hint = "ENTER abrir   D borrar   Q quitar del mapa   R grabar";
    std::string text;
    if (pending_delete_ > 0) {
        text = "D otra vez: borrar este track     ESC: no";
        hint = text.c_str();
    } else if (!message_.empty()) {
        hint = message_.c_str();
    } else if (selected_ == 0) {
        hint = rec.active ? "ENTER parar y guardar en ~/Tracks" : "ENTER empezar a grabar (sigue aunque salgas)";
    }
    lv_label_set_text(hint_, hint);
    lv_obj_set_style_text_color(hint_, pending_delete_ > 0 ? lv_color_hex(0xE53935) : colors.text_disabled, 0);
}

bool TracksScreen::handle_key(uint32_t key) {
    const int count = static_cast<int>(files_.size()) + 1;
    const bool confirming = pending_delete_ > 0;
    message_.clear();
    switch (key) {
        case LV_KEY_UP:
        case LV_KEY_LEFT:
        case 'f':
        case 'F':
        case 'z':
        case 'Z':
            pending_delete_ = -1;
            selected_ = (selected_ + count - 1) % count;
            break;
        case LV_KEY_DOWN:
        case LV_KEY_RIGHT:
        case 'x':
        case 'X':
        case 'c':
        case 'C':
            pending_delete_ = -1;
            selected_ = (selected_ + 1) % count;
            break;
        case LV_KEY_ENTER:
        case '\r':
        case ' ':
            pending_delete_ = -1;
            if (selected_ == 0) {
                toggle_recording();
            } else {
                open_selected();
                return true; // puede haber cambiado de pantalla
            }
            break;
        case 'r':
        case 'R':
            pending_delete_ = -1;
            toggle_recording();
            break;
        case 'd':
        case 'D':
        case LV_KEY_DEL:
        case LV_KEY_BACKSPACE:
            if (selected_ == 0) {
                break;
            }
            if (confirming && pending_delete_ == selected_) {
                const auto& file = files_[static_cast<std::size_t>(selected_ - 1)];
                const auto rec = gps_.recording_status();
                if (rec.active && same_file(rec.path, file.path)) {
                    message_ = "No se puede borrar: se está grabando";
                } else {
                    platform::delete_file(file.path);
                    info_cache_.erase(file.path);
                    message_ = "Borrado " + file.name;
                }
                reload();
            } else {
                pending_delete_ = selected_;
            }
            break;
        case 'q':
        case 'Q':
            pending_delete_ = -1;
            gps_.clear_overlay();
            message_ = "Track quitado del mapa";
            break;
        default:
            return false;
    }
    refresh();
    return true;
}

bool TracksScreen::handle_back() {
    if (pending_delete_ > 0) {
        pending_delete_ = -1;
        refresh();
        return true;
    }
    return false;
}

void TracksScreen::toggle_recording() {
    if (gps_.recording()) {
        const std::string path = gps_.stop_recording();
        message_ = path.empty() ? "Parado (sin puntos, no se guardó)"
                                : "Guardado en ~/Tracks/" + std::filesystem::path(path).filename().string();
        info_cache_.clear();
        reload();
    } else {
        gps_.start_recording();
        message_ = "Grabando… puedes volver al mapa";
    }
    update_timer();
}

void TracksScreen::open_selected() {
    if (selected_ <= 0 || selected_ > static_cast<int>(files_.size())) {
        return;
    }
    const auto& file = files_[static_cast<std::size_t>(selected_ - 1)];
    gps::Track track;
    std::string error;
    if (!gps::load_gpx_file(file.path, track, error) || track.points.empty()) {
        message_ = "No se pudo leer " + file.name;
        refresh();
        return;
    }
    map::BBox box{90.0, 180.0, -90.0, -180.0};
    for (const auto& p : track.points) {
        box.min_lat = std::min(box.min_lat, p.lat);
        box.max_lat = std::max(box.max_lat, p.lat);
        box.min_lon = std::min(box.min_lon, p.lon);
        box.max_lon = std::max(box.max_lon, p.lon);
    }
    auto& view = gps_.map_view();
    view.lat = (box.min_lat + box.max_lat) / 2.0;
    view.lon = (box.min_lon + box.max_lon) / 2.0;
    view.zoom = map::fit_zoom(box, kW - 20, kH - 20, 5, 17);
    view.follow = false;
    track.name = file.name;
    gps_.set_overlay(std::move(track));
    view_model().go_to(model::AppPage::Map);
}

void TracksScreen::update_timer() {
    const bool want = gps_.recording();
    if (want && !timer_) {
        timer_ = lv_timer_create(timer_cb, 1000, this);
    } else if (!want && timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
}

void TracksScreen::refresh_observer(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<TracksScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

void TracksScreen::timer_cb(lv_timer_t* timer) {
    auto* screen = static_cast<TracksScreen*>(lv_timer_get_user_data(timer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen

/*
 * SPDX-License-Identifier: MIT
 */

#include "map_screen.h"

#include "asset_manager.h"
#include "map_tiles.h"
#include "offline_zone.h"
#include "theme.h"
#include "track_recorder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace screen {
namespace {

constexpr int32_t kW = view::kScreenWidth;                          // 320
constexpr int32_t kH = view::kScreenHeight - view::kTitleBarHeight; // 140
constexpr int32_t kCx = kW / 2;
constexpr int32_t kCy = kH / 2;
constexpr int kZoomMin = 5;
constexpr int kZoomMax = 19; // la ortofoto llega; el topográfico se amplía desde z17
// Cuántos niveles por encima se busca una tesela para ampliarla si falta la del zoom actual
// (así a z18, o en una zona bajada sólo hasta z14, se ve algo en vez de un hueco).
constexpr int kMaxOverzoom = 4;
// Movimiento mínimo (px) para repintar: menos no se aprecia y gasta batería.
constexpr long long kMinMovePx = 2;
// Mientras se bajan teselas online se mira cada segundo si han llegado.
constexpr uint32_t kFetchPollMs = 1000;

uint16_t to565(lv_color_t c) {
    return lv_color_to_u16(c);
}

// Cambiar el texto de un rótulo hace que LVGL lo repinte aunque sea el mismo: se evita.
void set_text(lv_obj_t* label, const char* text) {
    const char* current = lv_label_get_text(label);
    if (!current || std::strcmp(current, text) != 0) {
        lv_label_set_text(label, text);
    }
}

std::string tile_key(const map::Tile& t) {
    return map::tile_cache_path(t);
}

} // namespace

MapScreen::MapScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets), gps_(view_model.gps()), tiles_(gps_.tile_roots()) {
    tiles_.set_layer(gps_.layer());
    gps_.acquire();
    init();
    this->view_model().set_page_handlers(this, [this](uint32_t key) { return handle_key(key); },
                                         []() { return false; }); // ESC vuelve al menú
}

MapScreen::~MapScreen() {
    view_model().clear_page_handlers(this);
    if (poll_timer_) {
        lv_timer_delete(poll_timer_);
    }
    fetcher_.cancel();
    gps_.save_now(); // dónde se quedó el mapa, para abrir ahí la próxima vez
    gps_.release();
}

void MapScreen::build_content(lv_obj_t* content) {
    canvas_ = lv_canvas_create(content);
    canvas_buf_.resize(LV_CANVAS_BUF_SIZE(kW, kH, 16, LV_DRAW_BUF_STRIDE_ALIGN));
    lv_canvas_set_buffer(canvas_, canvas_buf_.data(), kW, kH, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(canvas_, 0, 0);

    overlay_line_ = lv_line_create(content);
    lv_obj_set_style_line_width(overlay_line_, 3, 0);
    lv_obj_set_style_line_rounded(overlay_line_, true, 0);
    lv_obj_set_style_line_opa(overlay_line_, LV_OPA_80, 0);

    track_line_ = lv_line_create(content);
    lv_obj_set_style_line_width(track_line_, 3, 0);
    lv_obj_set_style_line_rounded(track_line_, true, 0);

    marker_ = lv_obj_create(content);
    lv_obj_remove_style_all(marker_);
    lv_obj_set_size(marker_, 12, 12);
    lv_obj_set_style_radius(marker_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(marker_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(marker_, 2, 0);
    lv_obj_add_flag(marker_, LV_OBJ_FLAG_HIDDEN);

    const lv_font_t* small = assets().load_standard_font(11, app::StandardFontWeight::Regular);
    const lv_font_t* small_bold = assets().load_standard_font(11);
    auto make_label = [&](const lv_font_t* font, lv_align_t align, int32_t x, int32_t y) {
        lv_obj_t* label = lv_label_create(content);
        lv_obj_set_style_text_font(label, font ? font : &lv_font_montserrat_12, 0);
        lv_obj_set_style_bg_opa(label, LV_OPA_60, 0);
        lv_obj_set_style_bg_color(label, lv_color_black(), 0);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_set_style_pad_hor(label, 4, 0);
        lv_obj_set_style_pad_ver(label, 1, 0);
        lv_obj_set_style_radius(label, 3, 0);
        lv_obj_align(label, align, x, y);
        return label;
    };
    status_ = make_label(small, LV_ALIGN_TOP_LEFT, 2, 2);
    rec_ = make_label(small_bold, LV_ALIGN_TOP_RIGHT, -2, 2);
    lv_obj_set_style_bg_color(rec_, lv_color_hex(0xC62828), 0);
    lv_obj_set_style_bg_opa(rec_, LV_OPA_COVER, 0);
    online_ = make_label(small, LV_ALIGN_BOTTOM_LEFT, 2, -2);
    attribution_ = make_label(small, LV_ALIGN_BOTTOM_RIGHT, -2, -2);

    // Si se viene de Tracks con un track cargado, la vista ya está puesta sobre él.
    apply_theme();
    lv_subject_add_observer_obj(view_model().dark_mode_subject(), theme_observer_cb, content, this);
    poll_timer_ = lv_timer_create(poll_timer_cb, map::map_refresh_ms(gps_.map_view().zoom), this);
    poll();
}

double MapScreen::gx(double lon) const {
    return map::lon_to_tile_x(lon, gps_.map_view().zoom) * map::kTileSize;
}

double MapScreen::gy(double lat) const {
    return map::lat_to_tile_y(lat, gps_.map_view().zoom) * map::kTileSize;
}

bool MapScreen::handle_key(uint32_t key) {
    switch (key) {
        case LV_KEY_UP:
        case 'f':
        case 'F':
            pan(0, -kH / 3);
            return true;
        case LV_KEY_DOWN:
        case 'x':
        case 'X':
            pan(0, kH / 3);
            return true;
        case LV_KEY_LEFT:
        case 'z':
        case 'Z':
            pan(-kW / 3, 0);
            return true;
        case LV_KEY_RIGHT:
        case 'c':
        case 'C':
            pan(kW / 3, 0);
            return true;
        case LV_KEY_ENTER:
        case '\r':
        case ' ':
            gps_.map_view().follow = true;
            poll();
            return true;
        // En el teclado del Cardputer , . + - sólo están en la capa de símbolos: el zoom va
        // también en Q (acercar) y A (alejar), una encima de la otra.
        case 'q':
        case 'Q':
        case '+':
        case '=':
        case '.':
            set_zoom(gps_.map_view().zoom + 1);
            return true;
        case 'a':
        case 'A':
        case '-':
        case '_':
        case ',':
            set_zoom(gps_.map_view().zoom - 1);
            return true;
        case 'r':
        case 'R':
            if (gps_.recording()) {
                gps_.stop_recording();
            } else {
                gps_.start_recording();
            }
            redraw(true);
            return true;
        case 'o':
        case 'O':
            gps_.set_online(!gps_.online());
            fetch_failed_.clear();
            redraw(true);
            return true;
        case 'm':
        case 'M':
            // Topográfico → satélite → IGN Base. Lo que se estuviera bajando era de la otra capa.
            fetcher_.cancel();
            fetch_request_.clear();
            fetch_failed_.clear();
            gps_.set_layer(map::next_layer(gps_.layer()));
            tiles_.set_layer(gps_.layer());
            redraw(true);
            return true;
        case 'd':
        case 'D':
            gps_.map_view().follow = false;
            view_model().go_to(model::AppPage::Download);
            return true;
        default:
            return false;
    }
}

void MapScreen::pan(int dx_px, int dy_px) {
    auto& view = gps_.map_view();
    view.follow = false;
    const int z = view.zoom;
    const double x = gx(view.lon) + dx_px;
    const double y = gy(view.lat) + dy_px;
    view.lon = map::tile_x_to_lon(x / map::kTileSize, z);
    view.lat = map::tile_y_to_lat(y / map::kTileSize, z);
    redraw(true);
}

void MapScreen::set_zoom(int zoom) {
    zoom = std::clamp(zoom, kZoomMin, kZoomMax);
    if (zoom == gps_.map_view().zoom) {
        return;
    }
    gps_.map_view().zoom = zoom;
    update_timer();
    redraw(true);
}

void MapScreen::update_timer() {
    if (!poll_timer_) {
        return;
    }
    uint32_t period = map::map_refresh_ms(gps_.map_view().zoom);
    if (fetcher_.running()) {
        period = std::min(period, kFetchPollMs);
    }
    if (period != timer_period_) {
        timer_period_ = period;
        lv_timer_set_period(poll_timer_, period);
    }
}

void MapScreen::poll() {
    fix_ = gps_.fix();
    gps_.remember(fix_);
    auto& view = gps_.map_view();
    if (fix_.valid && view.follow) {
        view.lat = fix_.lat;
        view.lon = fix_.lon;
    }
    check_fetch_done();
    redraw(false);
    update_timer();
}

void MapScreen::redraw(bool force) {
    if (!canvas_) {
        return;
    }
    const auto& view = gps_.map_view();
    const long long cx = std::llround(gx(view.lon));
    const long long cy = std::llround(gy(view.lat));
    // Sin fix se pinta la última posición conocida (en gris): al volver a encender el GPS
    // sabes dónde estabas y el mapa no salta a otro sitio.
    const auto& last = gps_.last_position();
    const bool have_pos = fix_.valid || last.valid;
    const double pos_lat = fix_.valid ? fix_.lat : last.lat;
    const double pos_lon = fix_.valid ? fix_.lon : last.lon;
    const long long fx = have_pos ? std::llround(gx(pos_lon)) : 0;
    const long long fy = have_pos ? std::llround(gy(pos_lat)) : 0;
    const std::uint64_t revision = gps_.track_revision();

    const bool moved = std::llabs(cx - drawn_cx_) >= kMinMovePx ||
                       std::llabs(cy - drawn_cy_) >= kMinMovePx;
    const bool fix_moved = fix_.valid != drawn_fix_valid_ ||
                           std::llabs(fx - drawn_fix_x_) >= kMinMovePx ||
                           std::llabs(fy - drawn_fix_y_) >= kMinMovePx;
    const bool changed = force || moved || fix_moved || view.zoom != drawn_zoom_ ||
                         revision != drawn_revision_;

    update_labels(); // los rótulos son baratos y llevan el tiempo de grabación
    if (!changed) {
        return;
    }
    drawn_cx_ = cx;
    drawn_cy_ = cy;
    drawn_zoom_ = view.zoom;
    drawn_revision_ = revision;
    drawn_fix_valid_ = fix_.valid;
    drawn_fix_x_ = fx;
    drawn_fix_y_ = fy;

    draw_tiles();
    update_lines();

    if (have_pos) {
        const auto colors = view::palette(view_model().is_dark_mode());
        lv_obj_set_style_bg_color(marker_, fix_.valid ? colors.primary : lv_color_hex(0x9E9E9E), 0);
        const long long px = fx - cx + kCx;
        const long long py = fy - cy + kCy;
        if (px >= -6 && px < kW + 6 && py >= -6 && py < kH + 6) {
            lv_obj_set_pos(marker_, static_cast<int32_t>(px) - 6, static_cast<int32_t>(py) - 6);
            lv_obj_remove_flag(marker_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(marker_, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_obj_add_flag(marker_, LV_OBJ_FLAG_HIDDEN);
    }

    request_missing_tiles();
}

void MapScreen::draw_tiles() {
    missing_visible_.clear();
    const int z = gps_.map_view().zoom;
    const long long ox = drawn_cx_ - kCx; // píxel global de la esquina superior izquierda
    const long long oy = drawn_cy_ - kCy;
    const long long n = 1LL << z;

    const long long tx0 = ox >> 8;
    const long long ty0 = oy >> 8;
    const long long tx1 = (ox + kW - 1) >> 8;
    const long long ty1 = (oy + kH - 1) >> 8;
    for (long long ty = ty0; ty <= ty1; ++ty) {
        for (long long tx = tx0; tx <= tx1; ++tx) {
            const int dst_x = static_cast<int>(tx * map::kTileSize - ox);
            const int dst_y = static_cast<int>(ty * map::kTileSize - oy);
            if (ty < 0 || ty >= n) {
                fill_missing(dst_x, dst_y);
                continue;
            }
            const long long wx = ((tx % n) + n) % n; // la longitud da la vuelta
            if (!blit_tile(static_cast<int>(wx), static_cast<int>(ty), dst_x, dst_y)) {
                fill_missing(dst_x, dst_y);
            }
        }
    }
    lv_obj_invalidate(canvas_);
}

// Copia la tesela (tx, ty) del zoom actual en el canvas; si no está, amplía la parte que le
// corresponde de una tesela de un zoom menor. Devuelve false si no hay nada que pintar.
bool MapScreen::blit_tile(int tx, int ty, int dst_x, int dst_y) {
    lv_draw_buf_t* buf = lv_canvas_get_draw_buf(canvas_);
    if (!buf) {
        return false;
    }
    auto* base = static_cast<uint8_t*>(buf->data);
    const uint32_t stride = buf->header.stride;
    const int z = gps_.map_view().zoom;

    for (int d = 0; d <= kMaxOverzoom && z - d >= 0; ++d) {
        const map::Tile src{z - d, tx >> d, ty >> d};
        const platform::DecodedTile* tile = tiles_.get(src);
        if (!tile || tile->w != map::kTileSize || tile->h != map::kTileSize) {
            if (d == 0 && z <= map::layer_info(tiles_.layer()).max_zoom) {
                missing_visible_.push_back(map::Tile{z, tx, ty});
            }
            continue;
        }
        const int mask = (1 << d) - 1;
        const int sx0 = ((tx & mask) * map::kTileSize) >> d;
        const int sy0 = ((ty & mask) * map::kTileSize) >> d;
        const int x_begin = std::max(0, dst_x);
        const int x_end = std::min(kW, dst_x + map::kTileSize);
        const int y_begin = std::max(0, dst_y);
        const int y_end = std::min(kH, dst_y + map::kTileSize);
        if (x_begin >= x_end || y_begin >= y_end) {
            return true;
        }
        for (int y = y_begin; y < y_end; ++y) {
            auto* row = reinterpret_cast<uint16_t*>(base + static_cast<std::size_t>(y) * stride);
            const int sy = sy0 + ((y - dst_y) >> d);
            const uint16_t* src_row = &tile->pixels[static_cast<std::size_t>(sy) * tile->w];
            if (d == 0) {
                std::memcpy(row + x_begin, src_row + (x_begin - dst_x),
                            static_cast<std::size_t>(x_end - x_begin) * sizeof(uint16_t));
            } else {
                for (int x = x_begin; x < x_end; ++x) {
                    row[x] = src_row[sx0 + ((x - dst_x) >> d)];
                }
            }
        }
        return true;
    }
    return false;
}

void MapScreen::fill_missing(int dst_x, int dst_y) {
    lv_draw_buf_t* buf = lv_canvas_get_draw_buf(canvas_);
    if (!buf) {
        return;
    }
    const auto colors = view::palette(view_model().is_dark_mode());
    const uint16_t bg = to565(colors.surface);
    const uint16_t line = to565(colors.border);
    auto* base = static_cast<uint8_t*>(buf->data);
    const uint32_t stride = buf->header.stride;
    const int x_begin = std::max(0, dst_x);
    const int x_end = std::min(kW, dst_x + map::kTileSize);
    const int y_begin = std::max(0, dst_y);
    const int y_end = std::min(kH, dst_y + map::kTileSize);
    for (int y = y_begin; y < y_end; ++y) {
        auto* row = reinterpret_cast<uint16_t*>(base + static_cast<std::size_t>(y) * stride);
        const bool edge_row = (y - dst_y) == 0;
        for (int x = x_begin; x < x_end; ++x) {
            const bool edge = edge_row || (x - dst_x) == 0;
            row[x] = edge ? line : bg;
        }
    }
}

void MapScreen::update_lines() {
    const double ox = static_cast<double>(drawn_cx_ - kCx);
    const double oy = static_cast<double>(drawn_cy_ - kCy);

    // Se descartan los puntos a menos de 2 px del anterior: no se ven y LVGL pinta cada tramo.
    auto build = [&](auto begin, auto end, auto lat_of, auto lon_of, std::vector<lv_point_precise_t>& out) {
        out.clear();
        for (auto it = begin; it != end; ++it) {
            lv_point_precise_t p;
            p.x = static_cast<lv_value_precise_t>(gx(lon_of(*it)) - ox);
            p.y = static_cast<lv_value_precise_t>(gy(lat_of(*it)) - oy);
            if (!out.empty() && std::fabs(p.x - out.back().x) < 2 && std::fabs(p.y - out.back().y) < 2) {
                continue;
            }
            out.push_back(p);
        }
    };

    const auto& overlay = gps_.overlay().points;
    build(overlay.begin(), overlay.end(), [](const gps::TrackPoint& p) { return p.lat; },
          [](const gps::TrackPoint& p) { return p.lon; }, overlay_px_);
    if (overlay_px_.size() >= 2) {
        lv_line_set_points(overlay_line_, overlay_px_.data(), overlay_px_.size());
        lv_obj_remove_flag(overlay_line_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(overlay_line_, LV_OBJ_FLAG_HIDDEN);
    }

    gps_.copy_recorded_points(recorded_);
    build(recorded_.begin(), recorded_.end(), [](const auto& p) { return p.first; },
          [](const auto& p) { return p.second; }, recorded_px_);
    if (recorded_px_.size() >= 2) {
        lv_line_set_points(track_line_, recorded_px_.data(), recorded_px_.size());
        lv_obj_remove_flag(track_line_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(track_line_, LV_OBJ_FLAG_HIDDEN);
    }
}

void MapScreen::update_labels() {
    const auto& view = gps_.map_view();
    char buf[96];
    if (fix_.valid) {
        std::snprintf(buf, sizeof(buf), "%.5f %.5f  %.0f m  z%d  %d sat%s", fix_.lat, fix_.lon,
                      fix_.alt_m, view.zoom, fix_.satellites, view.follow ? "" : "  · libre");
    } else {
        std::snprintf(buf, sizeof(buf), "Buscando satélites…%s  z%d%s",
                      gps_.last_position().valid ? " (gris: última posición)" : "", view.zoom,
                      view.follow ? "" : "  · libre");
    }
    set_text(status_, buf);

    const auto rec = gps_.recording_status();
    if (rec.active) {
        std::snprintf(buf, sizeof(buf), "● %s  %s", gps::format_distance(rec.length_m).c_str(),
                      gps::format_duration(rec.elapsed_s).c_str());
        set_text(rec_, buf);
        lv_obj_remove_flag(rec_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(rec_, LV_OBJ_FLAG_HIDDEN);
    }

    const auto& layer = map::layer_info(gps_.layer());
    std::snprintf(buf, sizeof(buf), "%s · %s", layer.name, layer.attribution);
    set_text(attribution_, buf);

    if (gps_.online()) {
        if (fetcher_.running()) {
            const auto p = fetcher_.progress();
            std::snprintf(buf, sizeof(buf), "online · bajando %zu/%zu", p.done + p.skipped + p.failed,
                          p.total);
        } else {
            std::snprintf(buf, sizeof(buf), "online");
        }
        set_text(online_, buf);
        lv_obj_remove_flag(online_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(online_, LV_OBJ_FLAG_HIDDEN);
    }
}

// Modo online: se bajan sólo las teselas que faltan en pantalla, al zoom actual.
void MapScreen::request_missing_tiles() {
    if (!gps_.online() || fetcher_.running()) {
        return;
    }
    std::vector<map::Tile> wanted;
    for (const auto& t : missing_visible_) {
        if (!fetch_failed_.count(tile_key(t))) {
            wanted.push_back(t);
        }
    }
    if (wanted.empty()) {
        return;
    }
    fetcher_.reset();
    fetch_request_ = wanted;
    fetcher_.start(std::move(wanted), gps_.tiles_dir(), gps_.layer());
    update_timer();
}

void MapScreen::check_fetch_done() {
    if (fetch_request_.empty() || fetcher_.running()) {
        return;
    }
    // Las que no llegaron no se vuelven a pedir en esta visita (sin red, fuera de España...).
    std::error_code ec;
    for (const auto& t : fetch_request_) {
        const auto path = std::filesystem::path(gps_.tiles_dir()) / map::tile_path(gps_.layer(), t);
        if (!std::filesystem::is_regular_file(path, ec)) {
            fetch_failed_.insert(tile_key(t));
        }
    }
    fetch_request_.clear();
    tiles_.invalidate_missing();
    redraw(true);
}

void MapScreen::apply_theme() {
    const auto colors = view::palette(view_model().is_dark_mode());
    lv_obj_set_style_line_color(track_line_, lv_color_hex(0xD32F2F), 0);
    lv_obj_set_style_line_color(overlay_line_, lv_color_hex(0x7B1FA2), 0);
    lv_obj_set_style_bg_color(marker_, colors.primary, 0);
    lv_obj_set_style_border_color(marker_, lv_color_white(), 0);
}

void MapScreen::poll_timer_cb(lv_timer_t* timer) {
    auto* screen = static_cast<MapScreen*>(lv_timer_get_user_data(timer));
    if (screen) {
        screen->poll();
    }
}

void MapScreen::theme_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<MapScreen*>(lv_observer_get_user_data(observer));
    if (screen && screen->canvas_) {
        screen->apply_theme();
        screen->redraw(true);
    }
}

} // namespace screen

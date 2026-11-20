/*
 * SPDX-License-Identifier: MIT
 */

#include "download_screen.h"

#include "asset_manager.h"
#include "theme.h"

#include <algorithm>
#include <cstdio>

namespace screen {
namespace {

constexpr int32_t kMargin = 6;
constexpr int32_t kW = view::kScreenWidth;
constexpr int32_t kRowHeight = 14;
constexpr int32_t kInfoTop = 2 + 6 * kRowHeight + 2;
constexpr int32_t kInfoLine = 12;
constexpr std::array<int, 8> kRadiiKm = {1, 2, 5, 10, 15, 20, 30, 50};
// Si la descarga se come más de esta parte del espacio libre, se avisa (pero se deja).
constexpr double kWarnFreeShare = 0.8;

const lv_color_t kWarnColor = lv_color_hex(0xF9A825);
const lv_color_t kErrorColor = lv_color_hex(0xE53935);
const lv_color_t kOkColor = lv_color_hex(0x43A047);

} // namespace

DownloadScreen::DownloadScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets), gps_(view_model.gps()) {
    storages_ = platform::storage_options();
    storage_ = -1;
    for (std::size_t i = 0; i < storages_.size(); ++i) {
        if (storages_[i].tiles_dir == gps_.tiles_dir()) {
            storage_ = static_cast<int>(i);
        }
    }
    if (storage_ < 0) {
        // Una carpeta puesta a mano en maps.conf que no es ninguna de las detectadas.
        storages_.push_back({"Carpeta de maps.conf", gps_.tiles_dir()});
        storage_ = static_cast<int>(storages_.size()) - 1;
    }
    recompute();
    init();
    this->view_model().set_page_handlers(this, [this](uint32_t key) { return handle_key(key); },
                                         [this]() { return handle_back(); });
}

DownloadScreen::~DownloadScreen() {
    view_model().clear_page_handlers(this);
    if (timer_) {
        lv_timer_delete(timer_);
    }
}

void DownloadScreen::build_content(lv_obj_t* content) {
    const lv_font_t* key_font = assets().load_standard_font(12);
    const lv_font_t* value_font = assets().load_standard_font(12, app::StandardFontWeight::Regular);
    const lv_font_t* info_font = assets().load_standard_font(11, app::StandardFontWeight::Regular);

    static constexpr std::array<const char*, kRows> kKeys = {"Mapa", "Zona", "Radio", "Zoom mín.",
                                                             "Zoom máx.", "Guardar en"};
    for (int i = 0; i < kRows; ++i) {
        auto* box = lv_obj_create(content);
        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, kW - 2 * kMargin, kRowHeight);
        lv_obj_set_pos(box, kMargin, 2 + i * kRowHeight);
        lv_obj_set_style_radius(box, 3, 0);
        lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        row_boxes_[i] = box;

        auto* key = lv_label_create(box);
        lv_label_set_text(key, kKeys[i]);
        lv_obj_set_style_text_font(key, key_font ? key_font : &lv_font_montserrat_12, 0);
        lv_obj_align(key, LV_ALIGN_LEFT_MID, 5, 0);
        row_keys_[i] = key;

        auto* value = lv_label_create(box);
        lv_obj_set_style_text_font(value, value_font ? value_font : &lv_font_montserrat_12, 0);
        lv_label_set_long_mode(value, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(value, 210);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(value, LV_ALIGN_RIGHT_MID, -5, 0);
        row_values_[i] = value;
    }

    for (int i = 0; i < 3; ++i) {
        auto* label = lv_label_create(content);
        lv_obj_set_style_text_font(label, info_font ? info_font : &lv_font_montserrat_12, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(label, kW - 2 * kMargin - 10);
        lv_obj_set_pos(label, kMargin + 5, kInfoTop + i * kInfoLine);
        info_[i] = label;
    }

    bar_ = lv_bar_create(content);
    lv_obj_set_size(bar_, kW - 2 * kMargin - 10, 10);
    lv_obj_set_pos(bar_, kMargin + 5, 30);
    lv_bar_set_range(bar_, 0, 1000);
    lv_obj_add_flag(bar_, LV_OBJ_FLAG_HIDDEN);

    hint_ = lv_label_create(content);
    lv_obj_set_style_text_font(hint_, info_font ? info_font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(hint_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(hint_, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(hint_, kW - 2 * kMargin);
    lv_obj_align(hint_, LV_ALIGN_BOTTOM_MID, 0, -2);

    lv_subject_add_observer_obj(view_model().dark_mode_subject(), refresh_observer, content, this);
}

map::BBox DownloadScreen::current_box() const {
    if (zone_ == 0) {
        const auto& view = gps_.map_view();
        return map::bbox_around(view.lat, view.lon, kRadiiKm[static_cast<std::size_t>(radius_index_)]);
    }
    return map::zone_presets()[static_cast<std::size_t>(zone_ - 1)].box;
}

std::string DownloadScreen::zone_name() const {
    if (zone_ == 0) {
        return "Alrededor del mapa";
    }
    return map::zone_presets()[static_cast<std::size_t>(zone_ - 1)].name;
}

std::vector<map::MapLayer> DownloadScreen::chosen_layers() const {
    std::vector<map::MapLayer> layers;
    if (layer_choice_ >= map::kLayerCount) {
        for (int i = 0; i < map::kLayerCount; ++i) {
            layers.push_back(static_cast<map::MapLayer>(i));
        }
    } else {
        layers.push_back(static_cast<map::MapLayer>(layer_choice_));
    }
    return layers;
}

std::string DownloadScreen::layers_name() const {
    if (layer_choice_ >= map::kLayerCount) {
        return "Los tres";
    }
    return map::layer_info(static_cast<map::MapLayer>(layer_choice_)).name;
}

int DownloadScreen::zmax_for(map::MapLayer layer) const {
    return std::min(zmax_, map::layer_info(layer).max_zoom);
}

int DownloadScreen::top_zoom() const {
    int top = map::kDownloadZoomMin;
    for (const auto layer : chosen_layers()) {
        top = std::max(top, map::layer_info(layer).max_zoom);
    }
    return top;
}

void DownloadScreen::recompute() {
    plan_ = map::ZonePlan{};
    plan_.zmin = zmin_;
    plan_.zmax = std::min(zmax_, top_zoom());
    for (const auto layer : chosen_layers()) {
        const auto one = map::plan_zone(current_box(), zmin_, zmax_for(layer), layer);
        plan_.tiles += one.tiles;
        plan_.bytes += one.bytes;
        for (std::size_t z = 0; z < plan_.per_zoom.size(); ++z) {
            plan_.per_zoom[z] += one.per_zoom[z];
        }
    }
    free_ = platform::free_bytes(storages_[static_cast<std::size_t>(storage_)].tiles_dir);
    const bool fits = free_ < 0 || plan_.bytes < free_;
    can_start_ = plan_.tiles > 0 && plan_.tiles <= map::kMaxTilesPerDownload && fits;
}

void DownloadScreen::change(int delta) {
    const int zones = static_cast<int>(map::zone_presets().size()) + 1;
    switch (row_) {
        case 0: {
            // Si el zoom máx. estaba en «máx.», sigue en «máx.» con el mapa nuevo.
            const bool at_top = zmax_ >= top_zoom();
            const int n = map::kLayerCount + 1;
            layer_choice_ = (layer_choice_ + delta + n) % n;
            if (at_top) {
                zmax_ = map::kDownloadZoomMax;
            }
            zmin_ = std::min(zmin_, top_zoom());
            break;
        }
        case 1:
            zone_ = (zone_ + delta + zones) % zones;
            break;
        case 2:
            if (zone_ == 0) {
                radius_index_ = std::clamp(radius_index_ + delta, 0, static_cast<int>(kRadiiKm.size()) - 1);
            }
            break;
        case 3:
            zmin_ = std::clamp(zmin_ + delta, map::kDownloadZoomMin, std::min(zmax_, top_zoom()));
            break;
        case 4:
            zmax_ = std::clamp(std::min(zmax_, top_zoom()) + delta, zmin_, top_zoom());
            break;
        case 5: {
            const int n = static_cast<int>(storages_.size());
            storage_ = (storage_ + delta + n) % n;
            break;
        }
        default:
            break;
    }
    recompute();
}

void DownloadScreen::start() {
    if (!can_start_) {
        return;
    }
    gps_.set_tiles_dir(storages_[static_cast<std::size_t>(storage_)].tiles_dir); // el mapa buscará primero aquí
    job_layers_ = chosen_layers();
    job_index_ = 0;
    job_box_ = current_box();
    job_total_ = static_cast<std::size_t>(plan_.tiles);
    acc_ = platform::TileDownloader::Progress{};
    state_ = State::Downloading;
    start_next_layer();
    if (!timer_) {
        timer_ = lv_timer_create(timer_cb, 500, this);
    }
}

void DownloadScreen::start_next_layer() {
    current_layer_ = job_layers_[job_index_];
    downloader_.reset();
    downloader_.start(map::zone_tiles(job_box_, zmin_, zmax_for(current_layer_), current_layer_),
                      storages_[static_cast<std::size_t>(storage_)].tiles_dir, current_layer_);
}

bool DownloadScreen::handle_key(uint32_t key) {
    if (state_ == State::Downloading) {
        return false; // sólo ESC (cancelar)
    }
    if (state_ == State::Result) {
        if (key == LV_KEY_ENTER || key == '\r' || key == ' ') {
            state_ = State::Setup;
            recompute();
            refresh();
            return true;
        }
        return false;
    }
    switch (key) {
        case LV_KEY_UP:
        case 'f':
        case 'F':
            row_ = (row_ + kRows - 1) % kRows;
            break;
        case LV_KEY_DOWN:
        case 'x':
        case 'X':
            row_ = (row_ + 1) % kRows;
            break;
        case LV_KEY_LEFT:
        case 'z':
        case 'Z':
            change(-1);
            break;
        case LV_KEY_RIGHT:
        case 'c':
        case 'C':
            change(1);
            break;
        case LV_KEY_ENTER:
        case '\r':
        case ' ':
            start();
            break;
        default:
            return false;
    }
    refresh();
    return true;
}

bool DownloadScreen::handle_back() {
    if (state_ == State::Downloading) {
        downloader_.cancel();
        return true;
    }
    if (state_ == State::Result) {
        state_ = State::Setup;
        recompute();
        refresh();
        return true;
    }
    return false;
}

void DownloadScreen::refresh() {
    if (!hint_) {
        return;
    }
    const auto colors = view::palette(view_model().is_dark_mode());
    char buf[160];

    if (state_ == State::Downloading && !downloader_.running()) {
        const auto p = downloader_.progress();
        if (p.finished && !p.cancelled && p.error.empty() && job_index_ + 1 < job_layers_.size()) {
            // Esta capa ha terminado bien: se suma y se pasa a la siguiente.
            acc_.done += p.done;
            acc_.skipped += p.skipped;
            acc_.failed += p.failed;
            acc_.bytes += p.bytes;
            ++job_index_;
            start_next_layer();
        } else {
            state_ = State::Result;
        }
    }
    if (state_ == State::Result && timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }

    const bool setup = state_ == State::Setup;
    for (int i = 0; i < kRows; ++i) {
        if (!setup) {
            lv_obj_add_flag(row_boxes_[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(row_boxes_[i], LV_OBJ_FLAG_HIDDEN);
        const bool selected = i == row_;
        lv_obj_set_style_bg_opa(row_boxes_[i], selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(row_boxes_[i], colors.primary, 0);
        lv_obj_set_style_text_color(row_keys_[i], selected ? colors.background : colors.text_disabled, 0);
        lv_obj_set_style_text_color(row_values_[i], selected ? colors.background : colors.text, 0);
    }

    if (setup) {
        lv_obj_add_flag(bar_, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < 3; ++i) {
            lv_obj_set_y(info_[i], kInfoTop + i * kInfoLine);
        }
        lv_label_set_text(row_values_[0], ("‹ " + layers_name() + " ›").c_str());
        lv_label_set_text(row_values_[1], ("‹ " + zone_name() + " ›").c_str());
        if (zone_ == 0) {
            std::snprintf(buf, sizeof(buf), "‹ %d km ›", kRadiiKm[static_cast<std::size_t>(radius_index_)]);
        } else {
            std::snprintf(buf, sizeof(buf), "zona entera");
        }
        lv_label_set_text(row_values_[2], buf);
        std::snprintf(buf, sizeof(buf), "‹ %d ›", zmin_);
        lv_label_set_text(row_values_[3], buf);
        if (zmax_ >= top_zoom()) {
            // Cada mapa hasta su máximo (topo 17, base 18, satélite 19).
            std::snprintf(buf, sizeof(buf), "‹ máx. › (%d)", top_zoom());
        } else {
            std::snprintf(buf, sizeof(buf), "‹ %d ›  (máx. %d)", zmax_, top_zoom());
        }
        lv_label_set_text(row_values_[4], buf);
        lv_label_set_text(row_values_[5], ("‹ " + storages_[static_cast<std::size_t>(storage_)].label + " ›").c_str());

        // Línea 1: tamaño. Se dice cuánto pesa el zoom mayor, que es casi todo.
        std::snprintf(buf, sizeof(buf), "%s teselas · ~%s   (z%d: %s)", map::format_count(plan_.tiles).c_str(),
                      map::format_bytes(plan_.bytes).c_str(), plan_.zmax,
                      map::format_count(plan_.per_zoom[static_cast<std::size_t>(plan_.zmax)]).c_str());
        lv_label_set_text(info_[0], buf);
        lv_obj_set_style_text_color(info_[0], colors.text, 0);

        // Línea 2: espacio libre.
        if (free_ >= 0) {
            std::snprintf(buf, sizeof(buf), "Libre: %s  ·  %s", map::format_bytes(free_).c_str(),
                          storages_[static_cast<std::size_t>(storage_)].tiles_dir.c_str());
        } else {
            std::snprintf(buf, sizeof(buf), "Libre: ¿?  ·  %s",
                          storages_[static_cast<std::size_t>(storage_)].tiles_dir.c_str());
        }
        lv_label_set_text(info_[1], buf);
        lv_obj_set_style_text_color(info_[1], colors.text_disabled, 0);

        // Línea 3: veredicto.
        lv_color_t verdict_color = kOkColor;
        if (plan_.tiles > map::kMaxTilesPerDownload) {
            std::snprintf(buf, sizeof(buf), "✖ Demasiado (máx. %s teselas): baja el zoom máx.",
                          map::format_count(map::kMaxTilesPerDownload).c_str());
            verdict_color = kErrorColor;
        } else if (free_ >= 0 && plan_.bytes >= free_) {
            std::snprintf(buf, sizeof(buf), "✖ No cabe: faltan %s", map::format_bytes(plan_.bytes - free_).c_str());
            verdict_color = kErrorColor;
        } else if (free_ >= 0 && plan_.bytes > static_cast<long long>(free_ * kWarnFreeShare)) {
            std::snprintf(buf, sizeof(buf), "⚠ Cabe justo: usaría el %.0f %% del espacio libre",
                          100.0 * static_cast<double>(plan_.bytes) / static_cast<double>(free_));
            verdict_color = kWarnColor;
        } else {
            std::snprintf(buf, sizeof(buf), "✔ Cabe. Lo que ya tengas no se vuelve a bajar.");
        }
        lv_label_set_text(info_[2], buf);
        lv_obj_set_style_text_color(info_[2], verdict_color, 0);

        lv_label_set_text(hint_, can_start_ ? "F/X fila   Z/C cambiar   ENTER descargar"
                                            : "F/X fila   Z/C cambiar");
        lv_obj_set_style_text_color(hint_, colors.text_disabled, 0);
        return;
    }

    // Descargando o resultado: lo de las capas terminadas más lo de la que va.
    const auto cur = downloader_.progress();
    platform::TileDownloader::Progress p = cur;
    p.done += acc_.done;
    p.skipped += acc_.skipped;
    p.failed += acc_.failed;
    p.bytes += acc_.bytes;
    p.total = std::max(job_total_, p.done + p.skipped + p.failed);
    const std::size_t processed = p.done + p.skipped + p.failed;
    const int permille = p.total ? static_cast<int>(1000.0 * processed / p.total) : 0;
    lv_obj_remove_flag(bar_, LV_OBJ_FLAG_HIDDEN);
    lv_bar_set_value(bar_, permille, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_, colors.border, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_, colors.primary, LV_PART_INDICATOR);

    lv_obj_set_y(info_[0], 8);
    lv_obj_set_y(info_[1], 50);
    lv_obj_set_y(info_[2], 50 + kInfoLine);

    if (state_ == State::Downloading) {
        if (job_layers_.size() > 1) {
            std::snprintf(buf, sizeof(buf), "Bajando %s (%zu/%zu) · %s (z%d–%d)",
                          map::layer_info(current_layer_).name, job_index_ + 1, job_layers_.size(),
                          zone_name().c_str(), zmin_, zmax_for(current_layer_));
        } else {
            std::snprintf(buf, sizeof(buf), "Bajando %s · %s (z%d–%d)", map::layer_info(current_layer_).name,
                          zone_name().c_str(), zmin_, zmax_for(current_layer_));
        }
    } else if (p.finished) {
        std::snprintf(buf, sizeof(buf), "✔ Hecho: %s · %s", layers_name().c_str(), zone_name().c_str());
    } else if (p.cancelled) {
        std::snprintf(buf, sizeof(buf), "Cancelado (lo bajado se queda)");
    } else {
        std::snprintf(buf, sizeof(buf), "✖ Parado: %s", p.error.c_str());
    }
    lv_label_set_text(info_[0], buf);
    lv_obj_set_style_text_color(info_[0],
                                state_ == State::Downloading || p.finished || p.cancelled ? colors.text : kErrorColor, 0);

    std::snprintf(buf, sizeof(buf), "%s / %s teselas  ·  %d %%", map::format_count(static_cast<long long>(processed)).c_str(),
                  map::format_count(static_cast<long long>(p.total)).c_str(), permille / 10);
    lv_label_set_text(info_[1], buf);
    lv_obj_set_style_text_color(info_[1], colors.text, 0);

    std::snprintf(buf, sizeof(buf), "Nuevas %s (%s) · ya estaban %s · fallos %s",
                  map::format_count(static_cast<long long>(p.done)).c_str(), map::format_bytes(p.bytes).c_str(),
                  map::format_count(static_cast<long long>(p.skipped)).c_str(),
                  map::format_count(static_cast<long long>(p.failed)).c_str());
    lv_label_set_text(info_[2], buf);
    lv_obj_set_style_text_color(info_[2], colors.text_disabled, 0);

    lv_label_set_text(hint_, state_ == State::Downloading ? "ESC cancelar (lo bajado se queda)"
                                                          : "ENTER / ESC volver");
    lv_obj_set_style_text_color(hint_, colors.text_disabled, 0);
}

void DownloadScreen::refresh_observer(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<DownloadScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

void DownloadScreen::timer_cb(lv_timer_t* timer) {
    auto* screen = static_cast<DownloadScreen*>(lv_timer_get_user_data(timer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen

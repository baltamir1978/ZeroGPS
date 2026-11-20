/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

#include <array>

namespace screen {

/**
 * El menú: dos tarjetas (Map y Tracks) del Cap LoRa+GPS. La
 * resaltada la lleva el viewmodel (`menu_index`); ENTER entra en ella.
 */
class HomeScreen : public BaseScreen {
public:
    HomeScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~HomeScreen() override;

protected:
    void build_content(lv_obj_t* content) override;

private:
    void refresh();
    static void refresh_observer(lv_observer_t* observer, lv_subject_t* subject);

    static constexpr std::size_t kMenuEntries = viewmodel::BaseViewModel::kMenuCount;
    std::array<lv_obj_t*, kMenuEntries> cards_{};
    std::array<lv_obj_t*, kMenuEntries> names_{};
    std::array<lv_obj_t*, kMenuEntries> subtitles_{};
    lv_obj_t* hint_{nullptr};
};

} // namespace screen

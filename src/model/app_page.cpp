/*
 * SPDX-License-Identifier: MIT
 */

// El «modelo» (lógica sin LVGL ni hardware) de ZeroGPS: NMEA, GPX y la matemática de teselas
// viven aquí, con sus pruebas en tests/.

#include "app_page.h"

namespace model {

// Número de funciones del menú; sirve de comprobación de que el enum está en orden.
int tool_count() {
    return static_cast<int>(AppPage::Download);
}

} // namespace model

/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

namespace model {

// ZeroGPS: un menú (Home) y una pantalla por función del Cap LoRa+GPS.
enum class AppPage {
    Home = 0,
    Map = 1,    // mapa en vivo (teselas del IGN) con la posición y el track
    Tracks = 2, // tracks GPX: grabar, ver, borrar
    Download = 3, // mapas offline: descargar una zona a la tarjeta
    Sky = 4,      // satélites: el cielo, la precisión y los datos del fix
};

} // namespace model

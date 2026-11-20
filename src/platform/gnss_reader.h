/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "gps_nmea.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace platform {

/**
 * El GNSS del Cap (AT6668) por `/dev/serial0` a 115200. Lee NMEA en un hilo y mantiene el
 * último fix. En el escritorio (y con `ZEROGPS_GNSS=sim` en el aparato) usa un backend
 * **simulado** que genera un recorrido en movimiento, para probar el mapa sin hardware.
 *
 * Batería: el hilo duerme en `poll()` hasta que llegan datos (nada de esperas activas) y al
 * parar se cortan los 5V del cap, que es lo que más gasta del GNSS. Quien lo usa
 * (`GpsSession`) sólo lo enciende mientras hace falta.
 */
class GnssReader {
public:
    GnssReader();
    ~GnssReader();

    GnssReader(const GnssReader&) = delete;
    GnssReader& operator=(const GnssReader&) = delete;

    bool is_simulated() const {
        return simulated_;
    }

    // Se llama desde el hilo del GNSS con cada fix nuevo (no tocar LVGL ahí). Ponerlo antes
    // de start().
    void set_on_fix(std::function<void(const gps::Fix&)> callback) {
        on_fix_ = std::move(callback);
    }

    void start();
    void stop();
    // Para y espera a que el hilo termine (antes de destruir lo que use `on_fix`).
    void stop_and_join() {
        stop();
        join_worker();
    }
    bool running() const;

    gps::Fix fix() const;

private:
    void join_worker();
    void run_simulated();
    void run_serial();

    void publish(const gps::Fix& fix);

    const bool simulated_;
    std::function<void(const gps::Fix&)> on_fix_;
    std::thread worker_;
    std::atomic<bool> stop_{false};

    mutable std::mutex mutex_;
    gps::Fix fix_;
    bool running_ = false;
};

} // namespace platform

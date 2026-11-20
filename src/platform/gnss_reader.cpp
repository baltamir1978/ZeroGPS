/*
 * SPDX-License-Identifier: MIT
 */

#include "gnss_reader.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

namespace platform {
namespace {

constexpr double kPi = 3.14159265358979323846;

bool wants_simulation() {
#if USE_DESKTOP
    return true;
#else
    const char* mode = std::getenv("ZEROGPS_GNSS");
    return mode != nullptr && std::strcmp(mode, "sim") == 0;
#endif
}

bool sleep_interruptible(int total_ms, std::atomic<bool>& stop) {
    int slept = 0;
    while (slept < total_ms) {
        if (stop) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        slept += 50;
    }
    return !stop;
}

void set_cap_power(bool on) {
    std::ofstream en("/sys/class/leds/ext_5v_out/brightness");
    if (en) {
        en << (on ? "1" : "0");
    }
}

} // namespace

void GnssReader::publish(const gps::Fix& fix) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fix_ = fix;
    }
    if (on_fix_) {
        on_fix_(fix);
    }
}

GnssReader::GnssReader() : simulated_(wants_simulation()) {}

GnssReader::~GnssReader() {
    stop();
    join_worker();
}

void GnssReader::join_worker() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

void GnssReader::start() {
    stop();
    join_worker();
    stop_ = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = true;
    }
    worker_ = std::thread(simulated_ ? &GnssReader::run_simulated : &GnssReader::run_serial, this);
}

void GnssReader::stop() {
    stop_ = true;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        fix_.valid = false; // apagado no hay posición «actual»
    }
}

bool GnssReader::running() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

gps::Fix GnssReader::fix() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return fix_;
}

void GnssReader::run_simulated() {
    // Puerto de Navacerrada: dentro de las teselas de Guadarrama que vienen con la app.
    double lat = 40.7930;
    double lon = -4.0120;
    double bearing = 0.4; // radianes
    int seconds = 0;

    while (!stop_) {
        bearing += 0.03; // curva suave
        const double step_deg = 0.00002; // ~2 m/s: a pie, como por el monte
        lat += std::cos(bearing) * step_deg;
        lon += std::sin(bearing) * step_deg / std::cos(lat * kPi / 180.0);
        ++seconds;

        gps::Fix fix;
        fix.valid = true;
        fix.lat = lat;
        fix.lon = lon;
        fix.alt_m = 1850.0;
        fix.fix_quality = 1;
        fix.speed_kmh = step_deg * 111000.0 * 3.6; // aprox
        fix.course_deg = std::fmod(bearing * 180.0 / kPi, 360.0);
        fix.year = 2026;
        fix.month = 11;
        fix.day = 20;
        fix.hour = 10 + (seconds / 3600);
        fix.minute = (seconds / 60) % 60;
        fix.second = seconds % 60;
        fix.fix_mode = 3;
        fix.hdop = 0.9;
        fix.vdop = 1.4;
        fix.pdop = 1.7;
        // Un cielo que gira despacio, con varias constelaciones, para ver la pantalla de
        // satélites sin el cap.
        struct SimSat {
            gps::System system;
            int prn;
            int elevation;
            int azimuth;
            int snr;
            bool used;
        };
        static constexpr SimSat kSimSky[] = {
            {gps::System::Gps, 2, 62, 40, 42, true},      {gps::System::Gps, 5, 18, 300, 31, true},
            {gps::System::Gps, 12, 35, 140, 38, true},    {gps::System::Gps, 25, 8, 210, -1, false},
            {gps::System::Galileo, 7, 48, 95, 40, true},  {gps::System::Galileo, 30, 22, 250, 27, false},
            {gps::System::Beidou, 23, 71, 200, 39, true}, {gps::System::Beidou, 37, 12, 60, 24, false},
            {gps::System::Glonass, 70, 30, 330, 33, true}, {gps::System::Sbas, 49, 33, 165, 35, false},
            {gps::System::Gps, 29, -1, -1, 22, false},
        };
        fix.sky.clear();
        for (const auto& s : kSimSky) {
            gps::Satellite sat;
            sat.system = s.system;
            sat.prn = s.prn;
            sat.elevation = s.elevation;
            sat.azimuth = s.azimuth < 0 ? -1 : (s.azimuth + seconds / 4) % 360;
            sat.snr = s.snr;
            sat.used = s.used;
            fix.sky.push_back(sat);
        }
        fix.satellites = 6;
        publish(fix);

        if (!sleep_interruptible(1000, stop_)) {
            break;
        }
    }
}

void GnssReader::run_serial() {
    // Sin los 5V del cap el GNSS no está alimentado.
    set_cap_power(true);

    // El puerto puede no estar listo nada más dar corriente (o estar ocupado): se reintenta
    // cada 2 s en vez de rendirse a la primera.
    int fd = -1;
    while (!stop_) {
        fd = ::open("/dev/serial0", O_RDONLY | O_NOCTTY | O_NONBLOCK);
        if (fd >= 0) {
            break;
        }
        if (!sleep_interruptible(2000, stop_)) {
            break;
        }
    }
    if (fd < 0) {
        set_cap_power(false);
        return;
    }

    termios tio{};
    if (tcgetattr(fd, &tio) == 0) {
        cfmakeraw(&tio);
        cfsetispeed(&tio, B115200);
        cfsetospeed(&tio, B115200);
        tio.c_cflag |= CLOCAL | CREAD;
        tcsetattr(fd, TCSANOW, &tio);
    }

    gps::Parser parser;
    std::string line;
    char buf[512];
    pollfd pfd{fd, POLLIN, 0};
    while (!stop_) {
        // Se duerme hasta que hay datos (el GNSS manda una ráfaga por segundo); el tope de
        // 500 ms es sólo para enterarse de stop().
        const int ready = ::poll(&pfd, 1, 500);
        if (ready <= 0 || !(pfd.revents & POLLIN)) {
            continue;
        }
        const ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n <= 0) {
            continue;
        }
        for (ssize_t i = 0; i < n; ++i) {
            const char c = buf[i];
            if (c == '\n' || c == '\r') {
                if (!line.empty()) {
                    // Se publica con cada RMC y GGA (una vez por segundo); lo que traen GSV y
                    // GSA (cielo, DOP) va dentro del mismo fix.
                    const bool is_gga = line.size() > 6 && line.compare(3, 3, "GGA") == 0;
                    const bool is_rmc = line.size() > 6 && line.compare(3, 3, "RMC") == 0;
                    if (parser.feed(line) && (is_gga || is_rmc)) {
                        publish(parser.fix());
                    }
                    line.clear();
                }
            } else if (line.size() < 200) {
                line += c;
            }
        }
    }

    ::close(fd);
    set_cap_power(false);
}

} // namespace platform

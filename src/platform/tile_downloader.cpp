/*
 * SPDX-License-Identifier: MIT
 */

#include "tile_downloader.h"

#include "storage.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace platform {
namespace {

namespace fs = std::filesystem;

constexpr std::size_t kBatch = 48;
// Por debajo de esto se para: no llenar la tarjeta del sistema hasta que falle todo.
constexpr long long kMinFreeBytes = 64LL * 1024 * 1024;
// Tandas seguidas sin ninguna tesela buena: no hay red (o el IGN no responde).
constexpr int kMaxDeadBatches = 2;

bool looks_like_jpeg(const fs::path& path, std::uintmax_t& size) {
    std::error_code ec;
    size = fs::file_size(path, ec);
    if (ec || size < 100) {
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    unsigned char magic[2] = {0, 0};
    in.read(reinterpret_cast<char*>(magic), 2);
    return magic[0] == 0xFF && magic[1] == 0xD8;
}

} // namespace

TileDownloader::~TileDownloader() {
    cancel();
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool TileDownloader::start(std::vector<map::Tile> tiles, std::string dir, map::MapLayer layer) {
    if (running()) {
        return false;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    tiles_ = std::move(tiles);
    dir_ = std::move(dir);
    layer_ = layer;
    cancel_ = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        progress_ = Progress{};
        progress_.total = tiles_.size();
        progress_.running = true;
    }
    worker_ = std::thread(&TileDownloader::run, this);
    return true;
}

void TileDownloader::cancel() {
    cancel_ = true;
    const pid_t pid = child_.load();
    if (pid > 0) {
        ::kill(pid, SIGTERM);
    }
}

bool TileDownloader::running() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return progress_.running;
}

TileDownloader::Progress TileDownloader::progress() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return progress_;
}

void TileDownloader::reset() {
    if (running()) {
        return;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    progress_ = Progress{};
}

void TileDownloader::run() {
    std::error_code ec;
    fs::create_directories(dir_, ec);

    std::vector<map::Tile> batch;
    int dead_batches = 0;
    std::string error;

    auto flush = [&]() {
        if (batch.empty()) {
            return true;
        }
        const long long free = free_bytes(dir_);
        if (free >= 0 && free < kMinFreeBytes) {
            error = "Sin espacio en la tarjeta";
            return false;
        }
        const bool any_ok = run_batch(batch);
        batch.clear();
        if (cancel_) {
            return false;
        }
        dead_batches = any_ok ? 0 : dead_batches + 1;
        if (dead_batches >= kMaxDeadBatches) {
            error = "Sin conexión (o el IGN no responde)";
            return false;
        }
        return true;
    };

    bool ok = true;
    for (const auto& tile : tiles_) {
        if (cancel_) {
            ok = false;
            break;
        }
        const fs::path target = fs::path(dir_) / map::tile_path(layer_, tile);
        if (fs::is_regular_file(target, ec)) {
            std::lock_guard<std::mutex> lock(mutex_);
            ++progress_.skipped;
            continue;
        }
        batch.push_back(tile);
        if (batch.size() >= kBatch && !(ok = flush())) {
            break;
        }
    }
    if (ok && !cancel_) {
        flush();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    progress_.running = false;
    progress_.cancelled = cancel_;
    progress_.finished = !cancel_ && error.empty();
    progress_.error = error;
}

bool TileDownloader::run_batch(const std::vector<map::Tile>& batch) {
    // Lista de url/output para `curl -K`: una sola ejecución de curl por tanda.
    const fs::path config = fs::path(dir_) / ".zerogps-batch.curl";
    {
        std::ofstream out(config, std::ios::trunc);
        for (const auto& tile : batch) {
            const fs::path part = fs::path(dir_) / (map::tile_path(layer_, tile) + ".part");
            out << "url = \"" << map::tile_url(layer_, tile) << "\"\n";
            out << "output = \"" << part.string() << "\"\n";
        }
    }

    const pid_t pid = ::fork();
    if (pid == 0) {
        // Hijo: sin salida por pantalla; los errores se ven en los ficheros que faltan.
        const int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            ::dup2(devnull, STDOUT_FILENO);
            ::dup2(devnull, STDERR_FILENO);
        }
        ::execlp("curl", "curl", "--silent", "--fail", "--parallel", "--parallel-max", "3",
                 "--create-dirs", "--retry", "2", "--retry-delay", "1", "--connect-timeout",
                 "10", "--max-time", "30", "--user-agent", "ZeroGPS (Cardputer Zero)", "-K",
                 config.c_str(), static_cast<char*>(nullptr));
        ::_exit(127);
    }
    if (pid < 0) {
        return false;
    }
    child_ = pid;
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    child_ = -1;

    std::error_code ec;
    fs::remove(config, ec);

    bool any_ok = false;
    for (const auto& tile : batch) {
        const fs::path target = fs::path(dir_) / map::tile_path(layer_, tile);
        const fs::path part = target.string() + ".part";
        std::uintmax_t size = 0;
        if (!cancel_ && looks_like_jpeg(part, size)) {
            fs::rename(part, target, ec);
            if (!ec) {
                any_ok = true;
                std::lock_guard<std::mutex> lock(mutex_);
                ++progress_.done;
                progress_.bytes += static_cast<long long>(size);
                continue;
            }
        }
        fs::remove(part, ec);
        if (!cancel_) {
            std::lock_guard<std::mutex> lock(mutex_);
            ++progress_.failed;
        }
    }
    return any_ok;
}

} // namespace platform

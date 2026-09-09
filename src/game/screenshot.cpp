#include "screenshot.h"

#include "application.h"
#include "logger.h"
#include "translation.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"

static const std::string CONSOLE_COMMAND_VIEWMODE_UNLIT = "viewmode unlit";
static const std::string CONSOLE_COMMAND_VIEWMODE_LIT = "viewmode lit";
static const std::string CONSOLE_COMMAND_TILEDSHOT = "tiledshot %d %d";

struct FileStamp {
        std::string name;
        int64_t size = 0;
        int64_t modified = 0;
};

int64_t fileStampTime(const std::filesystem::file_time_type& fileTime) {
    return std::chrono::duration_cast<std::chrono::seconds>(fileTime.time_since_epoch()).count();
}

FileStamp stampOf(const std::filesystem::directory_entry& entry) {
    FileStamp stamp;
    stamp.name = entry.path().filename().string();
    std::error_code ec;
    stamp.size = entry.file_size(ec);
    auto t = entry.last_write_time(ec);
    stamp.modified = fileStampTime(t);
    return stamp;
}

std::set<std::string> snapshotBmpNames(const std::filesystem::path& directory) {
    std::set<std::string> bmpNames;
    std::error_code ec;
    if (!std::filesystem::exists(directory, ec)) {
        return bmpNames;
    }

    for (auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (ec || !entry.is_regular_file()) {
            continue;
        }

        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".bmp") {
            bmpNames.insert(entry.path().filename().string());
        }
    }
    return bmpNames;
}

std::string lowerExtension(const std::string& filename) {
    std::string ext = std::filesystem::path(filename).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

// SAS_%F_%R_<multiplier>x[_unlit].ext — %R sanitized (':' illegal on Windows)
std::string makeShotBaseName(int multiplier, bool unlit) {
    std::time_t tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tmBuffer{};
    localtime_s(&tmBuffer, &tt);
    char date[16] = {};
    char clock[16] = {};
    std::strftime(date, sizeof(date), "%Y-%m-%d", &tmBuffer);
    std::strftime(clock, sizeof(clock), "%H-%M-%S", &tmBuffer);

    char base[128] = {};
    snprintf(base, sizeof(base), "SAS_%s_%s_%dx%s", date, clock, multiplier, unlit ? "_unlit" : "");
    return std::string(base);
}

std::filesystem::path uniqueShotPath(const std::filesystem::path& directory, const std::string& baseName, const std::string& ext) {
    std::filesystem::path path = directory / (baseName + "." + ext);
    std::error_code ec;

    for (int i = 2; i < 1000 && std::filesystem::exists(path, ec); ++i) {
        path = directory / (baseName + "_" + std::to_string(i) + "." + ext);
    }

    return path;
}

std::string Screenshot::getStatus() const {
    std::lock_guard<std::mutex> lock(statusMutex);
    return statusTextValue;
}

void Screenshot::setStatus(const std::string& status) {
    std::lock_guard<std::mutex> lock(statusMutex);
    statusTextValue = status;
}

std::filesystem::path Screenshot::defaultOutputDirectory() {
    wchar_t exePath[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (len == 0) {
        return {};
    }

    std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
    for (auto dir = exeDir; !dir.empty(); dir = dir.parent_path()) {
        std::filesystem::path bioGame = dir / "BioGame";
        std::error_code ec;
        if (std::filesystem::is_directory(bioGame, ec)) {
            std::filesystem::path shots = bioGame / "ScreenShots";
            std::filesystem::create_directories(shots, ec);
            return shots;
        }
    }
    return exeDir / "BioGame" / "ScreenShots";
}

bool Screenshot::convertBmpTo(const std::filesystem::path& source, const std::filesystem::path& destination, ScreenshotFormat format) {
    int w = 0;
    int h = 0;
    int comp = 0;
    stbi_uc* pixels = stbi_load(source.string().c_str(), &w, &h, &comp, 4);
    if (!pixels) {
        return false;
    }

    int ok = 0;
    switch (format) {
        case ScreenshotFormat::PNG: {
            ok = stbi_write_png(destination.string().c_str(), w, h, 4, pixels, w * 4);
            break;
        }
        case ScreenshotFormat::JPEG: {
            ok = stbi_write_jpg(destination.string().c_str(), w, h, 4, pixels, 98);
            break;
        }
        case ScreenshotFormat::BMP: {
            ok = stbi_write_bmp(destination.string().c_str(), w, h, 4, pixels);
        }
    }
    stbi_image_free(pixels);

    if (!ok) {
        Logger->error("screenshot: failed to convert {} to {}", source.string(), destination.string());
    }

    return ok != 0;
}

void Screenshot::start(int multiplier, int overlap, ScreenshotFormat format, std::string outDir, bool extraUnlit) {
    if (busyState.exchange(true)) {
        return;
    }

    cancelState.store(false);
    setStatus("");
    {
        std::lock_guard<std::mutex> lock(statusMutex);
        lastStatusOk = false;
        lastSavedPath.clear();
    }
    if (workerThread.joinable()) {
        workerThread.join();
    }

    workerThread = std::thread(&Screenshot::shot, this, multiplier, overlap, format, outDir, extraUnlit);
}

void Screenshot::shutdown() {
    cancelState.store(true);
    if (workerThread.joinable()) {
        workerThread.join();
    }
    busyState.store(false);
}

void Screenshot::shot(int multiplier, int overlap, ScreenshotFormat format, std::string outDir, bool extraUnlit) {
    const std::filesystem::path sourceDirectory = defaultOutputDirectory();
    std::filesystem::path targetDirectory = outDir.empty() ? sourceDirectory : std::filesystem::path(outDir);
    Logger->debug("screenshot: worker start sourceDir='{}' targetDir='{}' multiplier={} overlap={} extraUnlit={}", sourceDirectory.string(),
                  targetDirectory.string(), multiplier, overlap, extraUnlit ? 1 : 0);

    const int maxShots = extraUnlit ? 2 : 1;
    std::vector<std::filesystem::path> shotPaths;
    for (int shotIndex = 0; shotIndex < maxShots; ++shotIndex) {
        if (cancelState.load()) {
            break;
        }

        if (shotIndex == 1) {
            Application::instance().engine().consoleCommand(CONSOLE_COMMAND_VIEWMODE_UNLIT);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }

        setStatus(shotIndex == 0 ? t("ui.shot_table.status_capturing") : t("ui.shot_table.status_capturing_unlit"));
        std::set<std::string> before = snapshotBmpNames(sourceDirectory);

        char cmd[96];
        std::snprintf(cmd, sizeof(cmd), CONSOLE_COMMAND_TILEDSHOT.c_str(), multiplier, overlap);
        Application::instance().engine().consoleCommand(cmd);

        std::filesystem::path newFile;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        std::string lastSeen;
        int64_t lastSize = -1;
        int stablePasses = 0;
        while (std::chrono::steady_clock::now() < deadline && !cancelState.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            std::error_code ec;
            bool foundNew = false;

            for (const auto& entry : std::filesystem::directory_iterator(sourceDirectory, ec)) {
                if (ec || !entry.is_regular_file()) {
                    continue;
                }
                if (lowerExtension(entry.path().filename().string()) != ".bmp" || before.count(entry.path().filename().string()) != 0) {
                    continue;
                }

                FileStamp fs = stampOf(entry);
                if (fs.size <= 0) {
                    continue; // we can catch the file being written
                }

                foundNew = true;
                if (fs.size != lastSize || fs.name != lastSeen) {
                    lastSeen = fs.name;
                    lastSize = fs.size;
                    stablePasses = 0;
                    Logger->debug("screenshot: file changed lastSeen='{}' lastSize={} (not ready yet)", lastSeen, lastSize);
                    continue; // wait for the file to stabilize
                }

                if (++stablePasses >= 2) {
                    newFile = entry.path();
                    Logger->debug("screenshot: file stabilized lastSeen='{}' lastSize={} (ready)", lastSeen, lastSize);
                    break;
                }
            }
            if (!foundNew) {
                lastSeen.clear();
                lastSize = -1;
                stablePasses = 0;
            }

            if (!newFile.empty()) {
                break;
            }
        }

        if (newFile.empty()) {
            Logger->error("screenshot: failed to find a new screenshot {} file within the deadline", shotIndex + 1);
            setStatus(t("ui.shot_table.status_no_file"));
            break;
        }

        shotPaths.push_back(newFile);
        char capturedBuf[256] = {};
        snprintf(capturedBuf, sizeof(capturedBuf), t("ui.shot_table.status_captured"), newFile.filename().string().c_str());
        setStatus(capturedBuf);
    }

    if (maxShots > 1) {
        Application::instance().engine().consoleCommand(CONSOLE_COMMAND_VIEWMODE_LIT);
    }

    convertOutput(targetDirectory, shotPaths, format, multiplier, extraUnlit);
}

void Screenshot::convertOutput(std::filesystem::path targetDirectory, const std::vector<std::filesystem::path>& shotPaths, ScreenshotFormat format,
                               int multiplier, bool extraUnlit) {
    std::error_code ec;
    std::filesystem::create_directories(targetDirectory, ec);
    for (size_t i = 0; i < shotPaths.size(); ++i) {
        if (cancelState.load()) {
            break;
        }

        const auto& newFile = shotPaths[i];
        char convertingBuf[256] = {};
        snprintf(convertingBuf, sizeof(convertingBuf), t("ui.shot_table.status_converting"), newFile.filename().string().c_str());
        setStatus(convertingBuf);
        const char* ext = format == ScreenshotFormat::PNG ? "png" : format == ScreenshotFormat::JPEG ? "jpg" : "bmp";
        std::filesystem::path target = uniqueShotPath(targetDirectory, makeShotBaseName(multiplier, extraUnlit && i == 1), ext);

        bool ok = false;
        if (target != newFile) {
            ok = convertBmpTo(newFile, target, format);
            if (ok) {
                lastSavedPath = target.string();
            }
            std::filesystem::remove(newFile, ec);
        } else {
            ok = true;
            lastSavedPath = target.string();
        }

        if (!ok) {
            char convertFailedBuf[256] = {};
            snprintf(convertFailedBuf, sizeof(convertFailedBuf), t("ui.shot_table.status_convert_failed"), newFile.filename().string().c_str());
            setStatus(convertFailedBuf);
        }
        Logger->debug("screenshot: conversion result saved for '{}' = {}", newFile.string(), ok);
        char convertedBuf[256] = {};
        snprintf(convertedBuf, sizeof(convertedBuf), t("ui.shot_table.status_converted"), newFile.filename().string().c_str());
        setStatus(convertedBuf);
    }

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        if (!lastSavedPath.empty()) {
            char savedBuf[1024] = {};
            snprintf(savedBuf, sizeof(savedBuf), t("ui.shot_table.status_saved_to"), lastSavedPath.c_str());
            statusTextValue = savedBuf;
            lastStatusOk = true;
        } else if (statusTextValue.empty()) {
            statusTextValue = t("ui.shot_table.status_failed");
            lastStatusOk = false;
        }
    }

    busyState.store(false);

    Application::instance().engine().postGameThreadTask([this]() {
        std::string msg = getStatus();
        if (!msg.empty()) {
            Application::instance().ui().toastManager.addToastNotification(msg, lastSucceeded() ? ToastTypeSuccess : ToastTypeError, 3.0);
        }
    });
}
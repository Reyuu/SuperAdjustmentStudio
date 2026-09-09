#ifndef SAS_SCREENSHOT_H
#define SAS_SCREENSHOT_H

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

enum class ScreenshotFormat {
    PNG,
    JPEG,
    BMP
};

class Screenshot {
    public:
        bool isBusy() const {
            return busyState.load();
        }

        bool isCancelled() const {
            return cancelState.load();
        }

        std::string getStatus() const;
        bool lastSucceeded() const {
            std::lock_guard<std::mutex> lock(statusMutex);
            return lastStatusOk;
        }
        void start(int multiplier, int overlap, ScreenshotFormat format, std::string outDir, bool extraUnlit);
        void shot(int multiplier, int overlap, ScreenshotFormat format, std::string outDir, bool extraUnlit);
        static bool convertBmpTo(const std::filesystem::path& source, const std::filesystem::path& destination, ScreenshotFormat format);
        static std::filesystem::path defaultOutputDirectory();
        void shutdown();

    private:
        void setStatus(const std::string& status);
        void convertOutput(std::filesystem::path targetDirectory, const std::vector<std::filesystem::path>& shotPaths, ScreenshotFormat format, int multiplier,
                           bool extraUnlit);

        std::thread workerThread;
        mutable std::mutex statusMutex;
        std::string statusTextValue;
        std::string lastSavedPath;
        bool lastStatusOk = false;
        std::atomic<bool> busyState{false};
        std::atomic<bool> cancelState{false};
};

#endif // SAS_SCREENSHOT_H
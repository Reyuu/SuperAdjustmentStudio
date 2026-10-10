#ifndef SAS_REMOTE_H
#define SAS_REMOTE_H

// update check and translation download. one WinHTTP helper serves both, so
// there is no HTTP dependency to ship

#include <future>
#include <string>

inline constexpr const char* SAS_REPO_OWNER = "Reyuu";
inline constexpr const char* SAS_REPO_NAME = "SuperAdjustmentStudio";

// branch the translations.json comes from. the Crowdin pull lands on whatever
// branch is in development, point this there for fresher strings than the release
inline constexpr const char* SAS_TRANSLATIONS_REF = "main";

inline constexpr const char* SAS_RELEASES_PAGE = "https://github.com/Reyuu/SuperAdjustmentStudio/releases/latest";

class RemoteContent {
    public:
        static RemoteContent& instance();

        // kicks both jobs, once per launch. safe to call more than once
        void start();
        void poll();

        // manual "check for updates" button, ignores the daily gate. false while
        // a check is already in flight
        bool startUpdateCheck(bool manual = false);

        bool updateAvailable() const {
            return updateIsNewer;
        }
        const std::string& updateTag() const {
            return newestTag;
        }

        // tags are N.Nbeta / N.N.Nbeta / N.N, the numbers decide and the letters
        // are ignored. a version not starting with a digit (a local dev build)
        // has no order and compares equal
        static int compareTags(const char* a, const char* b);

    private:
        void verifyTranslationsAsync();

        std::future<std::string> updateFuture; // the newer tag, empty when there is none
        std::future<bool> translationFuture;   // true when the cache file was replaced

        std::string newestTag;
        bool updateIsNewer = false;
        bool manualCheck = false;
        std::string pendingUpdateToast;
        bool pendingTranslationToast = false;
};

#endif // SAS_REMOTE_H
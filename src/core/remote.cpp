#include "remote.h"

#include "application.h"
#include "json.hpp"
#include "logger.h"
#include "plugin.h"
#include "settings.h"
#include "translation.h"

#include <cassert>
#include <chrono>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <windows.h>
#include <winhttp.h>

using namespace std::chrono_literals;

namespace {

constexpr const char* CACHE_FILE_NAME = "translations.json";
constexpr const char* TRANSLATIONS_META_NAME = "translations.json.meta";
constexpr const char* UPDATES_META_NAME = "updates.meta";
constexpr long long CHECK_INTERVAL_SECONDS = 24 * 60 * 60;

// what the last check saw, so a launch does not ask again what yesterday already asked
struct CheckMeta {
        std::string ref;
        std::string sha;
        std::string plugin;
        long long checked = 0;
};

long long nowSeconds() {
    return static_cast<long long>(std::time(nullptr));
}

std::filesystem::path metaPath(const char* name) {
    return Settings::instance().configDir() / name;
}

// the .meta files are advisory, a missing or broken one just means we check again
CheckMeta readMeta(const std::filesystem::path& path) {
    CheckMeta meta;
    std::ifstream in(path);
    if (!in.is_open()) {
        return meta;
    }
    json parsed = json::parse(in, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return meta;
    }
    if (parsed.contains("ref") && parsed["ref"].is_string()) {
        meta.ref = parsed["ref"].get<std::string>();
    }
    if (parsed.contains("sha") && parsed["sha"].is_string()) {
        meta.sha = parsed["sha"].get<std::string>();
    }
    if (parsed.contains("plugin") && parsed["plugin"].is_string()) {
        meta.plugin = parsed["plugin"].get<std::string>();
    }
    if (parsed.contains("checked") && parsed["checked"].is_number_integer()) {
        meta.checked = parsed["checked"].get<long long>();
    }
    return meta;
}

bool writeMeta(const std::filesystem::path& path, const CheckMeta& meta) {
    json out;
    out["ref"] = meta.ref;
    out["sha"] = meta.sha;
    out["plugin"] = meta.plugin;
    out["checked"] = meta.checked;

    try {
        std::filesystem::create_directories(path.parent_path());
        const std::filesystem::path staging = path.string() + ".tmp";
        {
            std::ofstream file(staging, std::ios::binary);
            if (!file.is_open()) {
                return false;
            }
            file << out.dump();
        }
        return MoveFileExA(staging.string().c_str(), path.string().c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
    } catch (const std::exception&) {
        return false;
    }
}

bool dueForCheck(long long lastChecked) {
    return lastChecked <= 0 || nowSeconds() - lastChecked >= CHECK_INTERVAL_SECONDS;
}

const std::string UPDATES_URL = std::string("https://api.github.com/repos/") + SAS_REPO_OWNER + "/" + SAS_REPO_NAME + "/releases/latest";
const std::string TRANSLATIONS_URL =
    std::string("https://cdn.jsdelivr.net/gh/") + SAS_REPO_OWNER + "/" + SAS_REPO_NAME + "@" + SAS_TRANSLATIONS_REF + "/src/resources/translations.json";

// 3 KB of JSON that answers the only question worth asking first: did the branch move?
std::string commitProbeUrl() {
    return std::string("https://api.github.com/repos/") + SAS_REPO_OWNER + "/" + SAS_REPO_NAME + "/commits?sha=" + SAS_TRANSLATIONS_REF + "&per_page=1";
}

struct InternetHandle {
        HINTERNET handle = nullptr;
        ~InternetHandle() {
            if (handle) {
                WinHttpCloseHandle(handle);
            }
        }
};

std::wstring widen(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

// the Nth dot separated number of a version, 0 when it runs out. both operands
// start with a digit, see compareTags
int versionComponent(const char* text, int index) {
    while (*text) {
        if (!std::isdigit(static_cast<unsigned char>(*text))) {
            return 0;
        }
        int value = 0;
        while (std::isdigit(static_cast<unsigned char>(*text))) {
            value = value * 10 + (*text - '0');
            ++text;
        }
        if (index == 0) {
            return value;
        }
        --index;
        if (*text != '.') {
            return 0;
        }
        ++text;
    }
    return 0;
}

bool httpGet(const std::string& url, std::string& out, std::string& err) {
    out.clear();
    err.clear();

    const std::wstring wideUrl = widen(url);
    wchar_t host[256] = {};
    wchar_t path[2048] = {};
    wchar_t extra[2048] = {};
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.lpszHostName = host;
    components.dwHostNameLength = ARRAYSIZE(host);
    components.lpszUrlPath = path;
    components.dwUrlPathLength = ARRAYSIZE(path);
    components.lpszExtraInfo = extra;
    components.dwExtraInfoLength = ARRAYSIZE(extra);

    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &components)) {
        err = "malformed url";
        return false;
    }
    if (components.nScheme != INTERNET_SCHEME_HTTPS) {
        err = "only https is allowed";
        return false;
    }

    const std::wstring userAgent = widen(std::string(PLUGIN_NAME) + "/" + PLUGIN_VERSION); // the GitHub API rejects requests without one
    const std::wstring resource = std::wstring(path) + extra;

    InternetHandle session{WinHttpOpen(userAgent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.handle) {
        err = "WinHttpOpen failed";
        return false;
    }
    // short on purpose, the job runs in the background but is consumed on the
    // render thread, so a hung request would stall a frame
    WinHttpSetTimeouts(session.handle, 5000, 5000, 5000, 10000);

    InternetHandle connect{WinHttpConnect(session.handle, host, components.nPort, 0)};
    if (!connect.handle) {
        err = "WinHttpConnect failed";
        return false;
    }

    InternetHandle request{
        WinHttpOpenRequest(connect.handle, L"GET", resource.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    if (!request.handle) {
        err = "WinHttpOpenRequest failed";
        return false;
    }

    if (!WinHttpSendRequest(request.handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.handle, nullptr)) {
        err = "request failed (" + std::to_string(GetLastError()) + ")";
        return false;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                             WINHTTP_NO_HEADER_INDEX)) {
        err = "no status code";
        return false;
    }
    if (status != 200) {
        err = "http " + std::to_string(status);
        return false;
    }

    DWORD available = 0;
    while (WinHttpQueryDataAvailable(request.handle, &available) && available > 0) {
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request.handle, chunk.data(), available, &read)) {
            err = "read failed";
            out.clear();
            return false;
        }
        out.append(chunk.data(), read);
    }
    return true;
}

#ifdef SAS_DEBUG
void verifyCompareTags() {
    assert(RemoteContent::compareTags("0.7beta", "0.6.2beta") > 0);
    assert(RemoteContent::compareTags("0.6.2beta", "0.6.2beta") == 0);
    assert(RemoteContent::compareTags("0.6beta", "0.6.2beta") < 0);
    assert(RemoteContent::compareTags("0.6beta", "0.6") == 0);
    assert(RemoteContent::compareTags("0.1beta", "dev") == 0); // no order, no nagging
    assert(RemoteContent::compareTags("dev", "0.1beta") == 0);
    assert(RemoteContent::compareTags("0.6.2beta", "0.6") > 0);
    assert(RemoteContent::compareTags("0.6.2beta", "0.0dev") > 0); // a local build is simply older
}

// the sidecar decides whether a launch downloads the file or does nothing, so
// pin down its round trip and the day boundary
void verifyCheckState() {
    const long long now = nowSeconds();
    assert(dueForCheck(0)); // never checked, or the file is gone
    assert(!dueForCheck(now));
    assert(dueForCheck(now - CHECK_INTERVAL_SECONDS));
    assert(!dueForCheck(now - CHECK_INTERVAL_SECONDS + 60));

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "sas-remote-meta-check.json";
    std::filesystem::remove(path);

    CheckMeta written;
    written.ref = "main";
    written.sha = "abc1234";
    written.plugin = "0.6.2beta";
    written.checked = 1234567890;
    assert(writeMeta(path, written));

    const CheckMeta readBack = readMeta(path);
    assert(readBack.ref == written.ref && readBack.sha == written.sha && readBack.plugin == written.plugin && readBack.checked == written.checked);

    // a missing or unreadable sidecar has to read as "never checked", never as garbage
    std::filesystem::remove(path);
    const CheckMeta missing = readMeta(path);
    assert(missing.checked == 0 && missing.sha.empty() && missing.ref.empty());
    assert(dueForCheck(missing.checked));
}
#endif

} // namespace

RemoteContent& RemoteContent::instance() {
    static RemoteContent content;
    return content;
}

int RemoteContent::compareTags(const char* a, const char* b) {
    // a version not starting with a digit (a local dev build, a malformed tag) has
    // no order. guessing one either nags about every release or hides a real one
    if (!std::isdigit(static_cast<unsigned char>(*a)) || !std::isdigit(static_cast<unsigned char>(*b))) {
        return 0;
    }
    for (int i = 0; i < 3; ++i) {
        const int left = versionComponent(a, i);
        const int right = versionComponent(b, i);
        if (left != right) {
            return left < right ? -1 : 1;
        }
    }
    return 0;
}

void RemoteContent::start() {
    verifyTranslationsAsync();
    startUpdateCheck();
#ifdef SAS_DEBUG
    verifyCompareTags();
    verifyCheckState();
#endif
}

void RemoteContent::verifyTranslationsAsync() {
    if (translationFuture.valid()) {
        return;
    }
    // ponytail: poll() consumes the future, so a shutdown mid-request waits for it
    // (bounded by the timeouts above). detached thread with shared state if it hangs
    translationFuture = std::async(std::launch::async, []() -> bool {
        const std::filesystem::path cache = Settings::instance().configDir() / CACHE_FILE_NAME;
        const std::filesystem::path metaFile = metaPath(TRANSLATIONS_META_NAME);

        CheckMeta meta = readMeta(metaFile);
        // a new build, or one aimed at another branch, is worth checking straight away.
        // comparing both is free
        const bool identityChanged = !meta.ref.empty() && (meta.ref != SAS_TRANSLATIONS_REF || meta.plugin != PLUGIN_VERSION);
        if (!identityChanged && !dueForCheck(meta.checked)) {
            Logger->info("remote: translations verified today, nothing to do");
            return false;
        }

        // the commit is the cheap question (did anything move?), the file is the
        // expensive one and only gets asked when it did
        std::string body;
        std::string err;
        std::string sha;
        if (httpGet(commitProbeUrl(), body, err)) {
            json parsed = json::parse(body, nullptr, false);
            if (!parsed.is_discarded() && parsed.is_array() && !parsed.empty() && parsed[0].contains("sha") && parsed[0]["sha"].is_string()) {
                sha = parsed[0]["sha"].get<std::string>();
            }
        }
        if (sha.empty()) {
            Logger->info("remote: cannot read the " + std::string(SAS_TRANSLATIONS_REF) + " commit, trying again next launch: " + err);
            return false; // stamp stays put, so a blip costs one launch, not a day
        }

        const bool unchanged = !identityChanged && sha == meta.sha && std::filesystem::exists(cache);
        if (unchanged) {
            meta.checked = nowSeconds();
            writeMeta(metaFile, meta);
            Logger->info("remote: translations still current at " + sha.substr(0, 7));
            return false;
        }

        body.clear();
        if (!httpGet(TRANSLATIONS_URL, body, err)) {
            Logger->info("remote: translation download failed: " + err);
            return false;
        }

        // never hand a broken file to the loader, the embedded copy has to stay in place
        json parsed = json::parse(body, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object() || parsed.empty()) {
            Logger->info("remote: downloaded translations are not a JSON object, keeping the embedded ones");
            return false;
        }

        try {
            std::filesystem::create_directories(cache.parent_path());

            const std::filesystem::path staging = cache.string() + ".tmp";
            {
                std::ofstream out(staging, std::ios::binary);
                if (!out.is_open()) {
                    Logger->info("remote: cannot open the translation cache for writing");
                    return false;
                }
                out << body;
            }

            // replace in one step, a half written cache would poison every later start
            if (!MoveFileExA(staging.string().c_str(), cache.string().c_str(), MOVEFILE_REPLACE_EXISTING)) {
                Logger->info("remote: cannot replace the translation cache");
                return false;
            }
        } catch (const std::exception& e) {
            Logger->info(std::string("remote: cannot cache the translations: ") + e.what());
            return false;
        }

        meta.ref = SAS_TRANSLATIONS_REF;
        meta.sha = sha;
        meta.plugin = PLUGIN_VERSION;
        meta.checked = nowSeconds();
        writeMeta(metaFile, meta);

        Logger->info("remote: translations updated from " + std::string(SAS_TRANSLATIONS_REF) + " at " + sha.substr(0, 7));
        return true;
    });
}

bool RemoteContent::startUpdateCheck(bool manual) {
    if (updateFuture.valid() && updateFuture.wait_for(0ms) != std::future_status::ready) {
        return false;
    }
    manualCheck = manual;
    // same daily window as the translations, so a launch costs two small requests
    // at most. the button ignores it
    const std::filesystem::path metaFile = metaPath(UPDATES_META_NAME);
    if (!manual && !dueForCheck(readMeta(metaFile).checked)) {
        return false;
    }
    // the worker only reports the newest tag, the comparison stays on the render
    // thread so every branch is in one place
    updateFuture = std::async(std::launch::async, [metaFile]() -> std::string {
        std::string body;
        std::string err;
        if (!httpGet(UPDATES_URL, body, err)) {
            Logger->info("remote: update check failed: " + err);
            return {};
        }

        json parsed = json::parse(body, nullptr, false);
        if (parsed.is_discarded() || !parsed.contains("tag_name") || !parsed["tag_name"].is_string()) {
            Logger->info("remote: update check got no tag_name, staying quiet");
            return {};
        }

        CheckMeta meta;
        meta.checked = nowSeconds();
        meta.plugin = PLUGIN_VERSION;
        writeMeta(metaFile, meta);

        const std::string tag = parsed["tag_name"].get<std::string>();
        Logger->info("remote: latest release is " + tag + ", ours is " + PLUGIN_VERSION);
        return tag;
    });
    return true;
}

void RemoteContent::poll() {
    if (translationFuture.valid() && translationFuture.wait_for(0ms) == std::future_status::ready) {
        bool applied = false;
        try {
            applied = translationFuture.get();
        } catch (const std::exception& e) {
            Logger->info(std::string("remote: translation worker threw: ") + e.what());
        } catch (...) {
            Logger->info("remote: translation worker threw unknown");
        }
        translationFuture = {};

        if (applied) {
            Translation::instance().loadTranslations(); // render thread, so nothing sees the swap half done
            pendingTranslationToast = true;
        }
    }

    if (updateFuture.valid() && updateFuture.wait_for(0ms) == std::future_status::ready) {
        std::string tag;
        try {
            tag = updateFuture.get();
        } catch (const std::exception& e) {
            Logger->info(std::string("remote: update worker threw: ") + e.what());
        } catch (...) {
            Logger->info("remote: update worker threw unknown");
        }
        updateFuture = {};

        const bool manual = manualCheck;
        manualCheck = false;
        const bool reachable = !tag.empty();
        // a 0.0dev build reads as 0.0.0, so it is older than any release and gets told so
        const bool newer = reachable && compareTags(tag.c_str(), PLUGIN_VERSION) > 0;

        // not t(), that macro hands out a dangling c_str() on a temporary
        Translation& translation = Translation::instance();
        if (newer) {
            newestTag = tag;
            updateIsNewer = true;
            pendingUpdateToast = translation.translate("ui.settings_table.update_available") + ": " + tag;
        } else if (manual) {
            // the button always answers, a silent press reads as a broken one
            if (!reachable) {
                pendingUpdateToast = translation.translate("ui.settings_table.check_failed");
            } else {
                pendingUpdateToast = translation.translate("ui.settings_table.up_to_date");
            }
        }
    }

    // toasts render only inside renderOverlayContents, hold them until the overlay
    // is up instead of expiring unseen
    if (pendingUpdateToast.empty() && !pendingTranslationToast) {
        return;
    }
    if (!Application::instance().ui().showUI().load()) {
        return;
    }

    ToastNotificationManager& toasts = Application::instance().ui().toastManager;
    if (!pendingUpdateToast.empty()) {
        toasts.addToastNotification(pendingUpdateToast, ToastTypeInfo, 8.0f);
        pendingUpdateToast.clear();
    }
    if (pendingTranslationToast) {
        toasts.addToastNotification(Translation::instance().translate("ui.settings_table.translations_updated"), ToastTypeInfo, 4.0f);
        pendingTranslationToast = false;
    }
}
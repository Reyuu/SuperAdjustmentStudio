#include "util.h"

// UE3 FString -> length-prefixed UTF-16 buffer
std::string FStringToUtf8(const FString& fStr) {
    std::wstring wStr = (std::wstring)fStr;
    if (wStr.empty()) {
        return {};
    }

    UINT n = LESDK::GetUtf8LengthWide(wStr.c_str(), (UINT)wStr.size());
    if (n == 0) {
        return {};
    }

    std::string out(n, '\0');
    DWORD err = 0;
    if (!LESDK::EncodeUtf8FromWide(wStr.c_str(), (UINT)wStr.size(), &out[0], n, &err)) {
        return {};
    }

    return out;
};

std::string WStringToUtf8(const std::wstring& wStr) {
    if (wStr.empty()) {
        return {};
    }

    UINT n = LESDK::GetUtf8LengthWide(wStr.c_str(), (UINT)wStr.size());
    if (n == 0) {
        return {};
    }

    std::string out(n, L'\0');
    DWORD err = 0;
    if (!LESDK::EncodeUtf8FromWide(wStr.c_str(), (UINT)wStr.size(), &out[0], n, &err)) {
        return {};
    }
    return out;
};

std::wstring toWString(const std::string& str) {
    if (str.empty()) {
        return {};
    }

    UINT n = LESDK::GetWideLengthUtf8(str.c_str(), (UINT)str.size());
    if (n == 0) {
        return {};
    }

    std::wstring out(n, L'\0');
    DWORD err = 0;
    if (!LESDK::EncodeWideFromUtf8(str.c_str(), (UINT)str.size(), &out[0], n, &err)) {
        return {};
    }
    return out;
};

std::string toLowerStr(const std::string& str) {
    std::string out = str;
    std::transform(out.begin(), out.end(), out.begin(), ::tolower);
    return out;
};
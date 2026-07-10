#include "platform/ads_enumerator.h"

#if defined(DATASCYTHE_PLATFORM_WINDOWS)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace datascythe {

namespace {

std::wstring utf8_to_wide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int required =
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(required - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), required);
    return out;
}

std::string wide_to_utf8(const wchar_t* wide) {
    if (!wide || wide[0] == L'\0') {
        return {};
    }
    const int required =
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(required - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), required, nullptr, nullptr);
    return out;
}

}  

std::vector<std::string> enumerate_alternate_data_streams(const std::string& file_path) {
    std::vector<std::string> streams;
    const std::wstring wide_path = utf8_to_wide(file_path);
    if (wide_path.empty()) {
        return streams;
    }

    WIN32_FIND_STREAM_DATA data{};
    const HANDLE find =
        FindFirstStreamW(wide_path.c_str(), FindStreamInfoStandard, &data, 0);
    if (find == INVALID_HANDLE_VALUE) {
        return streams;
    }

    do {
        const std::string name = wide_to_utf8(data.cStreamName);
        
        if (name.empty() || name == "::$DATA") {
            continue;
        }

        std::string stream_name = name;
        if (stream_name.size() >= 7 && stream_name.rfind(":$DATA") == stream_name.size() - 7) {
            stream_name = stream_name.substr(0, stream_name.size() - 7);
        }

        streams.push_back(file_path + ":" + stream_name);
    } while (FindNextStreamW(find, &data));

    FindClose(find);
    return streams;
}

}  

#endif
#include "Log.h"

#include <windows.h>
#include <shlobj.h>

#include <cwchar>
#include <mutex>

namespace
{
constexpr uintmax_t kMaxLogSize = uintmax_t{ 1024 } * 1024;
}

std::filesystem::path DataDirectory()
{
    std::filesystem::path directory;
    PWSTR localAppData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData)))
        directory = std::filesystem::path(localAppData) / L"MonitorControl";
    CoTaskMemFree(localAppData);

    std::error_code error;
    std::filesystem::create_directories(directory, error);
    return directory;
}

std::filesystem::path LogFilePath()
{
    return DataDirectory() / L"log.txt";
}

std::string ToUtf8(std::wstring_view text)
{
    if (text.empty())
        return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

void Log(std::wstring_view message)
{
    static std::mutex mutex;
    std::scoped_lock lock(mutex);

    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t stamp[32];
    swprintf_s(stamp, L"%04u-%02u-%02u %02u:%02u:%02u  ", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);

    std::wstring line = stamp;
    line += message;
    line += L"\r\n";
    OutputDebugStringW(line.c_str());

    const std::filesystem::path path = LogFilePath();
    std::error_code error;
    if (std::filesystem::file_size(path, error) > kMaxLogSize && !error)
        std::filesystem::rename(path, std::filesystem::path(path).replace_extension(L".old.txt"), error);

    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    const std::string utf8 = ToUtf8(line);
    DWORD written = 0;
    WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(file);
}

#define UNICODE
#define _UNICODE
#include <windows.h>
#include <string>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR commandLine, int)
{
    wchar_t modulePath[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, 32768);
    if (!length || length >= 32768) return 1;
    std::wstring root(modulePath, length);
    const auto separator = root.find_last_of(L"\\/");
    if (separator == std::wstring::npos) return 1;
    root.resize(separator);
    const std::wstring dist = root + L"\\dist";
    const std::wstring application = dist + L"\\Listify.exe";
    if (GetFileAttributesW(application.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(nullptr, L"dist\\Listify.exe est introuvable.", L"Listify", MB_OK | MB_ICONERROR);
        return 2;
    }
    std::wstring arguments = L"\"" + application + L"\"";
    if (commandLine && *commandLine) arguments += L" " + std::wstring(commandLine);
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USECOUNTCHARS;
    startup.dwXCountChars = 140; startup.dwYCountChars = 40;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(application.c_str(), arguments.data(), nullptr, nullptr, FALSE,
                        CREATE_NEW_CONSOLE, nullptr, dist.c_str(), &startup, &process)) return 3;
    CloseHandle(process.hThread); CloseHandle(process.hProcess); return 0;
}

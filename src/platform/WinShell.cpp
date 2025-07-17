#include "WinShell.hpp"
#include <shobjidl.h>
#include <filesystem>

namespace fs = std::filesystem;
namespace screenshot_tool {

    std::wstring WinShell::GetStartupFolder() {
        PWSTR path = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Startup, 0, nullptr, &path))) {
            std::wstring ret(path);
            CoTaskMemFree(path);
            return ret;
        }
        return L"";
    }

    bool WinShell::CreateStartupShortcut(const std::wstring& exePath, const std::wstring& linkName) {
        std::wstring startup = WinShell::GetStartupFolder();
        if (startup.empty()) return false;
        std::wstring linkPath = startup + L"\\" + linkName;

        Microsoft::WRL::ComPtr<IShellLinkW> sl;
        Microsoft::WRL::ComPtr<IPersistFile> pf;
        if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl)))) return false;
        sl->SetPath(exePath.c_str());
        sl->SetArguments(L"");
        sl->SetDescription(L"HDR Screenshot Tool");
        if (FAILED(sl.As(&pf))) return false;
        return SUCCEEDED(pf->Save(linkPath.c_str(), TRUE));
    }

    bool WinShell::RemoveStartupShortcut(const std::wstring& linkName) {
        std::wstring startup = WinShell::GetStartupFolder();
        if (startup.empty()) return false;
        std::wstring linkPath = startup + L"\\" + linkName;
        std::error_code ec;
        return std::filesystem::remove(linkPath, ec);
    }

    bool WinShell::IsStartupShortcutPresent(const std::wstring& linkName) {
        std::wstring startup = WinShell::GetStartupFolder();
        if (startup.empty()) return false;
        std::wstring linkPath = startup + L"\\" + linkName;
        return std::filesystem::exists(linkPath);
    }

} // namespace screenshot_tool
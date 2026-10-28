// The first-run setup's Windows parts. See doa4_setup_win.h.

#include "doa4_setup_win.h"

#include <windows.h>

#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <system_error>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace doa4::setup {

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

namespace {

// COM for the calling thread, released on scope exit when this call started
// it (as in doa4_photo_win.cpp).
class ComScope {
 public:
  ComScope() : hr_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
  ~ComScope() {
    if (SUCCEEDED(hr_)) CoUninitialize();
  }
  bool ok() const { return SUCCEEDED(hr_) || hr_ == RPC_E_CHANGED_MODE; }

 private:
  HRESULT hr_;
};

}  // namespace

std::optional<fs::path> PickIsoFile(void* owner_hwnd, const fs::path& start) {
  ComScope com;
  if (!com.ok()) return std::nullopt;
  ComPtr<IFileOpenDialog> dialog;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&dialog)))) {
    return std::nullopt;
  }
  const COMDLG_FILTERSPEC types[] = {{L"Disc images (*.iso)", L"*.iso"}, {L"All files", L"*.*"}};
  dialog->SetFileTypes(2, types);
  dialog->SetFileTypeIndex(1);
  DWORD options = 0;
  dialog->GetOptions(&options);
  dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
  dialog->SetTitle(L"Choose your Dead or Alive 4 disc image");
  std::error_code ec;
  if (!start.empty() && fs::is_directory(start, ec)) {
    ComPtr<IShellItem> folder;
    if (SUCCEEDED(SHCreateItemFromParsingName(start.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
      dialog->SetFolder(folder.Get());
    }
  }
  if (FAILED(dialog->Show(static_cast<HWND>(owner_hwnd)))) return std::nullopt;  // or cancelled
  ComPtr<IShellItem> result;
  PWSTR chosen = nullptr;
  if (FAILED(dialog->GetResult(&result)) ||
      FAILED(result->GetDisplayName(SIGDN_FILESYSPATH, &chosen)) || !chosen) {
    return std::nullopt;
  }
  fs::path path(chosen);
  CoTaskMemFree(chosen);
  return path;
}

std::optional<uint64_t> FreeBytes(const fs::path& target) {
  std::error_code ec;
  fs::path dir = target;
  while (!dir.empty() && !fs::is_directory(dir, ec)) {
    const fs::path parent = dir.parent_path();
    if (parent == dir) return std::nullopt;
    dir = parent;
  }
  if (dir.empty()) return std::nullopt;
  ULARGE_INTEGER free{};
  if (!GetDiskFreeSpaceExW(dir.c_str(), &free, nullptr, nullptr)) return std::nullopt;
  return free.QuadPart;
}

}  // namespace doa4::setup

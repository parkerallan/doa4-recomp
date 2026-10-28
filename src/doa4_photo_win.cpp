// Full-quality photos: the Windows parts. See doa4_photo_win.h.

#include "doa4_photo_win.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <knownfolders.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <format>
#include <system_error>

#include "doa4_utf8.h"

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

namespace doa4::photo {

namespace {

using Microsoft::WRL::ComPtr;

// COM for the calling thread, released on scope exit when this call started it.
class ComScope {
 public:
  explicit ComScope(DWORD model) : hr_(CoInitializeEx(nullptr, model)) {}
  ~ComScope() {
    if (SUCCEEDED(hr_)) CoUninitialize();
  }
  // RPC_E_CHANGED_MODE: COM is already up on this thread in the other model;
  // usable as it is.
  bool ok() const { return SUCCEEDED(hr_) || hr_ == RPC_E_CHANGED_MODE; }

 private:
  HRESULT hr_;
};

std::string HrText(const char* what, HRESULT hr) {
  return std::format("{} failed (0x{:08X})", what, uint32_t(hr));
}

std::string Encode(const Image& image, const std::filesystem::path& path, FileType type) {
  ComPtr<IWICImagingFactory> factory;
  HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory));
  if (FAILED(hr)) return HrText("creating the image encoder", hr);
  ComPtr<IWICStream> stream;
  hr = factory->CreateStream(&stream);
  if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
  if (FAILED(hr)) return HrText("opening the file", hr);
  ComPtr<IWICBitmapEncoder> encoder;
  hr = factory->CreateEncoder(type == FileType::kPng ? GUID_ContainerFormatPng
                                                     : GUID_ContainerFormatJpeg,
                              nullptr, &encoder);
  if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
  if (FAILED(hr)) return HrText("starting the encoder", hr);
  ComPtr<IWICBitmapFrameEncode> frame;
  ComPtr<IPropertyBag2> properties;
  hr = encoder->CreateNewFrame(&frame, &properties);
  if (SUCCEEDED(hr) && type == FileType::kJpg) {
    PROPBAG2 option{};
    option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_R4;
    value.fltVal = 0.95f;
    hr = properties->Write(1, &option, &value);
  }
  if (SUCCEEDED(hr)) hr = frame->Initialize(properties.Get());
  if (SUCCEEDED(hr)) hr = frame->SetSize(image.width, image.height);
  WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
  if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
  if (FAILED(hr)) return HrText("setting up the image", hr);
  if (format != GUID_WICPixelFormat24bppBGR) return "the encoder does not take 24-bit pixels";
  // R G B X -> B G R, the layout both encoders take.
  const UINT stride = image.width * 3;
  std::vector<uint8_t> bgr(size_t(stride) * image.height);
  for (size_t i = 0, j = 0; i < image.rgbx.size(); i += 4, j += 3) {
    bgr[j + 0] = image.rgbx[i + 2];
    bgr[j + 1] = image.rgbx[i + 1];
    bgr[j + 2] = image.rgbx[i + 0];
  }
  hr = frame->WritePixels(image.height, stride, UINT(bgr.size()), bgr.data());
  if (SUCCEEDED(hr)) hr = frame->Commit();
  if (SUCCEEDED(hr)) hr = encoder->Commit();
  if (SUCCEEDED(hr)) hr = stream->Commit(STGC_DEFAULT);
  if (FAILED(hr)) return HrText("writing the image", hr);
  return {};
}

}  // namespace

std::string WriteImage(const Image& image, const std::filesystem::path& path, FileType type) {
  if (image.width == 0 || image.height == 0 ||
      image.rgbx.size() != size_t(image.width) * image.height * 4) {
    return "there is no image to save";
  }
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) return "cannot create " + Utf8(path.parent_path()) + ": " + ec.message();
  ComScope com(COINIT_MULTITHREADED);
  if (!com.ok()) return "Windows imaging is not available";
  std::filesystem::path tmp = path;
  tmp += ".tmp";
  std::string error = Encode(image, tmp, type);
  if (error.empty()) {
    std::filesystem::rename(tmp, path, ec);
    if (ec) error = "cannot write " + Utf8(path) + ": " + ec.message();
  }
  if (!error.empty()) std::filesystem::remove(tmp, ec);
  return error;
}

bool ReadImage(const std::filesystem::path& path, Image* image) {
  ComScope com(COINIT_MULTITHREADED);
  if (!com.ok()) return false;
  ComPtr<IWICImagingFactory> factory;
  ComPtr<IWICBitmapDecoder> decoder;
  ComPtr<IWICBitmapFrameDecode> frame;
  ComPtr<IWICFormatConverter> converter;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&factory))) ||
      FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                WICDecodeMetadataCacheOnDemand, &decoder)) ||
      FAILED(decoder->GetFrame(0, &frame)) || FAILED(factory->CreateFormatConverter(&converter)) ||
      FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat24bppBGR,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom))) {
    return false;
  }
  UINT w = 0, h = 0;
  if (FAILED(converter->GetSize(&w, &h)) || !w || !h) return false;
  std::vector<uint8_t> bgr(size_t(w) * h * 3);
  if (FAILED(converter->CopyPixels(nullptr, w * 3, UINT(bgr.size()), bgr.data()))) return false;
  image->width = w;
  image->height = h;
  image->rgbx.assign(size_t(w) * h * 4, 0);
  for (size_t i = 0, j = 0; j < bgr.size(); i += 4, j += 3) {
    image->rgbx[i + 0] = bgr[j + 2];
    image->rgbx[i + 1] = bgr[j + 1];
    image->rgbx[i + 2] = bgr[j + 0];
  }
  return true;
}

std::string PathText(const std::filesystem::path& path) { return Utf8(path); }

std::filesystem::path DefaultPhotoDir() {
  PWSTR pictures = nullptr;
  std::filesystem::path dir;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &pictures)) && pictures) {
    dir = std::filesystem::path(pictures) / "DOA4";
  }
  CoTaskMemFree(pictures);
  return dir;
}

std::optional<std::filesystem::path> PickFolder(void* owner_hwnd,
                                                const std::filesystem::path& start,
                                                const wchar_t* title) {
  ComScope com(COINIT_APARTMENTTHREADED);
  if (!com.ok()) return std::nullopt;
  ComPtr<IFileOpenDialog> dialog;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&dialog)))) {
    return std::nullopt;
  }
  DWORD options = 0;
  dialog->GetOptions(&options);
  dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
  dialog->SetTitle(title);
  std::error_code ec;
  if (!start.empty() && std::filesystem::is_directory(start, ec)) {
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
  std::filesystem::path path(chosen);
  CoTaskMemFree(chosen);
  return path;
}

void OpenInExplorer(const std::filesystem::path& dir) {
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

Size MonitorSize(void* hwnd) {
  HMONITOR monitor = hwnd ? MonitorFromWindow(static_cast<HWND>(hwnd), MONITOR_DEFAULTTOPRIMARY)
                          : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(monitor, &info)) return {};
  return Size{uint32_t(info.rcMonitor.right - info.rcMonitor.left),
              uint32_t(info.rcMonitor.bottom - info.rcMonitor.top)};
}

}  // namespace doa4::photo

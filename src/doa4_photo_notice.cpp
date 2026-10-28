// "Photo saved" notice. See doa4_photo_notice.h.

#include "doa4_photo_notice.h"

#include <imgui.h>

#include <utility>

namespace doa4 {

PhotoNotice::PhotoNotice(rex::ui::ImGuiDrawer* drawer, std::string text, bool error,
                         std::function<void()> close)
    : rex::ui::ImGuiDialog(drawer),
      text_(std::move(text)),
      error_(error),
      close_(std::move(close)),
      shown_(std::chrono::steady_clock::now()) {}

void PhotoNotice::OnDraw(ImGuiIO& io) {
  if (!closing_ && std::chrono::steady_clock::now() - shown_ >= kShowFor) {
    closing_ = true;
    if (close_) close_();
  }
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 16.0f, 16.0f), ImGuiCond_Always,
                          ImVec2(1.0f, 0.0f));
  ImGui::SetNextWindowBgAlpha(0.8f);
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                 ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                 ImGuiWindowFlags_NoNav;
  if (ImGui::Begin("##doa4_photo_notice", nullptr, flags)) {
    if (error_) {
      ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", text_.c_str());
    } else {
      ImGui::TextUnformatted(text_.c_str());
    }
  }
  ImGui::End();
}

}  // namespace doa4

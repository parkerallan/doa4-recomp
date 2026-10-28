// A small "Photo saved" notice in the corner for a few seconds. The app
// creates it when a photo result arrives and destroys it (through `close`)
// once it has been shown long enough; while any ImGui dialog exists the
// presenter paints from the UI thread, so it exists only while shown.

#pragma once

#include <chrono>
#include <functional>
#include <string>

#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>

namespace doa4 {

class PhotoNotice : public rex::ui::ImGuiDialog {
 public:
  static constexpr std::chrono::milliseconds kShowFor{2000};

  // `close` is called once, from OnDraw, when the notice has been shown for
  // kShowFor; it must destroy the notice later (not from inside OnDraw).
  PhotoNotice(rex::ui::ImGuiDrawer* drawer, std::string text, bool error,
              std::function<void()> close);

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  std::string text_;
  bool error_;
  std::function<void()> close_;
  std::chrono::steady_clock::time_point shown_;
  bool closing_ = false;
};

}  // namespace doa4

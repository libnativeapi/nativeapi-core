// Tray view example: a menu bar item drawn by native views instead of an icon
// and a title. A Row holds a clock that ticks every second, a click counter and
// a "+1" button; the item grows and shrinks with its content. Click the clock
// or the counter and the tray icon's own click event fires; right-click for a
// menu that switches back to the icon and title, pins the width, or quits.
//
// Every state change prints a "[tray_view]" line with the item's bounds, so a
// GUI test can assert on it from outside.
//
// macOS shows the view. Windows and Linux only record it and keep showing the
// icon, which is what their notification areas can draw.

#include <atomic>
#include <chrono>
#include <ctime>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include "../../src/foundation/dispatcher.h"
#include "nativeapi.h"

using namespace nativeapi;

namespace {

std::string CurrentTime() {
  std::time_t now = std::time(nullptr);
  char buffer[16];
  std::strftime(buffer, sizeof(buffer), "%H:%M:%S", std::localtime(&now));
  return buffer;
}

}  // namespace

int main() {
  if (!TrayManager::GetInstance().IsSupported() || !View::IsSupported()) {
    std::cerr << "Tray icons or views are not supported on this platform." << std::endl;
    return 1;
  }

  auto& app = Application::GetInstance();

  auto tray = std::make_shared<TrayIcon>();
  // The icon and title are what the item falls back to without a content view,
  // and all that Windows and Linux show.
  tray->SetIcon(Image::FromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAYAAABzenr0AAAAOElEQVR4nO3RwQkAMAgEQcu205ST1CB5"
      "nI9ZuKcwYJVW1eeO9nsHAAAAALAPoPgLAAAAAADiAAV6s6MvzK1rffgAAAAASUVORK5CYII="));
  tray->SetTitle("Tray View");
  tray->SetTooltip("nativeapi tray view example");

  // --- The content view: [ 12:34:56 | Clicks: 0 | +1 ] ---
  auto content = std::make_shared<View>();
  content->SetLayout(ViewLayout::Row);
  content->SetPadding(EdgeInsets{0, 8, 0, 8});
  content->SetSpacing(8);
  content->SetBackgroundColor(Color::FromRGBA(40, 120, 220));

  auto clock = std::make_shared<Label>(CurrentTime());
  clock->SetTextColor(Color::FromRGBA(255, 255, 255));
  clock->SetAlignment(ViewAlignment::Center);

  auto counter = std::make_shared<Label>("Clicks: 0");
  counter->SetTextColor(Color::FromRGBA(255, 255, 255));
  counter->SetAlignment(ViewAlignment::Center);

  auto plus = std::make_shared<Button>("+1");
  plus->SetAlignment(ViewAlignment::Center);

  content->AddSubview(clock);
  content->AddSubview(counter);
  content->AddSubview(plus);

  auto print_bounds = [tray](const std::string& what) {
    const Rectangle b = tray->GetBounds();
    std::cout << "[tray_view] " << what << " bounds " << b.x << " " << b.y << " " << b.width
              << " " << b.height << std::endl;
  };

  // The counter's text gets longer as it grows past 9, 99, ...; the item
  // widens with it.
  auto clicks = std::make_shared<int>(0);
  plus->AddListener<ButtonClickedEvent>([=](const ButtonClickedEvent&) {
    ++*clicks;
    counter->SetText("Clicks: " + std::to_string(*clicks));
    print_bounds("button clicked, clicks " + std::to_string(*clicks));
  });

  // Clicks on the labels and the background fall through to the tray icon.
  tray->AddListener<TrayIconClickedEvent>([=](const TrayIconClickedEvent&) {
    print_bounds("tray icon clicked");
  });

  // --- Context menu ---
  auto menu = std::make_shared<Menu>();

  auto show_view = std::make_shared<MenuItem>("Show Custom View", MenuItemType::Checkbox);
  show_view->SetState(MenuItemState::Checked);
  show_view->AddListener<MenuItemClickedEvent>([=](const MenuItemClickedEvent&) {
    const bool showing = tray->GetContentView() != nullptr;
    tray->SetContentView(showing ? nullptr : content);
    show_view->SetState(showing ? MenuItemState::Unchecked : MenuItemState::Checked);
    print_bounds(showing ? "icon and title" : "custom view");
  });
  menu->AddItem(show_view);

  // A preferred width pins the item; 0 lets it follow the content again.
  auto fixed_width = std::make_shared<MenuItem>("Fixed Width (240 pt)", MenuItemType::Checkbox);
  fixed_width->AddListener<MenuItemClickedEvent>([=](const MenuItemClickedEvent&) {
    const bool fixed = content->GetPreferredSize().width > 0;
    content->SetPreferredSize(Size{fixed ? 0.0 : 240.0, 0});
    fixed_width->SetState(fixed ? MenuItemState::Unchecked : MenuItemState::Checked);
    print_bounds(fixed ? "width follows content" : "width fixed");
  });
  menu->AddItem(fixed_width);

  menu->AddSeparator();
  auto quit = std::make_shared<MenuItem>("Quit", MenuItemType::Normal);
  quit->AddListener<MenuItemClickedEvent>([&app](const MenuItemClickedEvent&) { app.Quit(0); });
  menu->AddItem(quit);

  tray->SetContextMenu(menu);
  tray->SetContextMenuTrigger(ContextMenuTrigger::RightClicked);

  tray->SetContentView(content);
  tray->SetVisible(true);

  // Views are main-thread only: the ticker thread posts each update there.
  // The first tick also reports where the item landed; right after
  // SetVisible() the menu bar has not placed it yet.
  std::atomic<bool> running{true};
  std::thread ticker([&running, clock, print_bounds] {
    bool first = true;
    while (running) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      RunOnMainThread([clock, print_bounds, first] {
        clock->SetText(CurrentTime());
        if (first) {
          print_bounds("started");
        }
      });
      first = false;
    }
  });

  std::cout << "Tray view example running. Right-click the item for the menu." << std::endl;
  const int exit_code = app.Run();

  running = false;
  ticker.join();
  return exit_code;
}

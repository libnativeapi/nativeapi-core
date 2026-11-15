#include "../src/application.h"
#include "../src/view.h"
#include "../src/window.h"

#if defined(__linux__) && !defined(__ANDROID__)
#include <gtk/gtk.h>
#endif

#include <iostream>
#include <vector>

using nativeapi::Application;
using nativeapi::Button;
using nativeapi::Label;
using nativeapi::Rectangle;
using nativeapi::Size;
using nativeapi::TextField;
using nativeapi::View;
using nativeapi::ViewLayout;
using nativeapi::ViewResizedEvent;
using nativeapi::WindowScaleFactorChangedEvent;

int main(int argc, char** argv) {
  int failures = 0;
  auto check = [&](bool ok, const char* what) {
    if (!ok) {
      std::cerr << "FAIL: " << what << std::endl;
      ++failures;
    }
  };

  // Event payload contracts.
  WindowScaleFactorChangedEvent scale_ev(42, 2.0);
  check(scale_ev.GetWindowId() == 42 && scale_ev.GetScaleFactor() == 2.0 &&
            scale_ev.GetTypeName() == "WindowScaleFactorChangedEvent",
        "WindowScaleFactorChangedEvent payload");

  ViewResizedEvent resize_ev(7, Size{320, 240});
  check(resize_ev.GetViewId() == 7 && resize_ev.GetNewSize().width == 320 &&
            resize_ev.GetNewSize().height == 240 && resize_ev.GetTypeName() == "ViewResizedEvent",
        "ViewResizedEvent payload");

#if defined(__linux__) && !defined(__ANDROID__)
  if (!gtk_init_check(&argc, &argv)) {
    return failures == 0 ? 77 : 1;
  }
#else
  (void)argc;
  (void)argv;
#endif
  Application::GetInstance();

  if (!View::IsSupported()) {
    std::cout << "view_api_test: View unsupported on this platform (skipped runtime checks)"
              << std::endl;
    return failures == 0 ? 0 : 1;
  }

  auto container = std::make_shared<View>();
  check(!container->GetAccessibilityLabel().has_value(), "default accessibility label empty");
  container->SetAccessibilityLabel("Main container");
  check(container->GetAccessibilityLabel() == std::optional<std::string>("Main container"),
        "set accessibility label");
  container->SetAccessibilityLabel(std::nullopt);
  check(!container->GetAccessibilityLabel().has_value(), "clear accessibility label");

  // ViewResizedEvent fires on size change, not on pure translation, and after Row/Column layout.
  std::vector<Size> observed_sizes;
  auto listener_id = container->AddListener<ViewResizedEvent>(
      [&](const ViewResizedEvent& e) { observed_sizes.push_back(e.GetNewSize()); });
  container->SetFrame(Rectangle{10, 20, 200, 120});
  check(observed_sizes.size() == 1 && observed_sizes[0].width == 200 &&
            observed_sizes[0].height == 120,
        "ViewResizedEvent on SetFrame resize");
  container->SetFrame(Rectangle{50, 60, 200, 120});
  check(observed_sizes.size() == 1, "ViewResizedEvent suppressed on pure move");

  container->SetLayout(ViewLayout::Row);
  auto child = std::make_shared<View>();
  child->SetFlex(1.0);
  std::vector<Size> child_sizes;
  child->AddListener<ViewResizedEvent>(
      [&](const ViewResizedEvent& e) { child_sizes.push_back(e.GetNewSize()); });
  container->AddSubview(child);
  check(child_sizes.size() == 1 && child_sizes[0].width == 200 && child_sizes[0].height == 120,
        "ViewResizedEvent on child laid out by Row");
  container->SetFrame(Rectangle{50, 60, 300, 140});
  check(child_sizes.size() == 2 && child_sizes[1].width == 300 && child_sizes[1].height == 140,
        "ViewResizedEvent on child after parent resize");
  container->RemoveListener(listener_id);

  // Label, Button, TextField font & border properties.
  auto label = std::make_shared<Label>("Hello");
  label->SetFontFamily("monospace");
  label->SetFontWeight(600);
  label->SetFontSize(14.0);
  check(label->GetFontFamily() == std::optional<std::string>("monospace") &&
            label->GetFontWeight() == 600 && label->GetFontSize() == 14.0,
        "Label font properties");
  label->SetFontWeight(9999);
  check(label->GetFontWeight() == 900, "Label font weight upper clamp");
  label->SetFontWeight(0);
  label->SetFontFamily(std::nullopt);
  check(label->GetFontWeight() == 0 && !label->GetFontFamily().has_value(),
        "Label font reset");

  auto button = std::make_shared<Button>("Action");
  button->SetFontFamily("system-ui");
  button->SetFontWeight(700);
  button->SetFontSize(13.0);
  button->SetAccessibilityLabel("Primary action");
  check(button->GetFontFamily() == std::optional<std::string>("system-ui") &&
            button->GetFontWeight() == 700 && button->GetFontSize() == 13.0 &&
            button->GetAccessibilityLabel() == std::optional<std::string>("Primary action"),
        "Button font & accessibility properties");

  auto field = std::make_shared<TextField>("initial");
  check(field->IsBordered(), "TextField bordered by default");
  field->SetBordered(false);
  check(!field->IsBordered(), "TextField SetBordered(false)");
  field->SetFontFamily("Menlo");
  field->SetFontWeight(500);
  field->SetFontSize(12.0);
  field->SetAccessibilityLabel("Search query");
  field->SetSecure(true);
  check(!field->IsBordered() && field->IsSecure() &&
            field->GetFontFamily() == std::optional<std::string>("Menlo") &&
            field->GetFontWeight() == 500 &&
            field->GetAccessibilityLabel() == std::optional<std::string>("Search query"),
        "TextField preserves unbordered, font & accessibility across SetSecure");
  field->SetSecure(false);
  field->SetMultiline(true);
  check(!field->IsBordered() && field->IsMultiline(), "TextField preserves unbordered in multiline");

  if (failures == 0) {
    std::cout << "view_api_test: all passed" << std::endl;
  }
  return failures == 0 ? 0 : 1;
}

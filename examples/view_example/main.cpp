// View example: a sign-in form built from native controls — no UI framework,
// just nativeapi. Type a name, press Enter or click the button, and watch the
// status label; the window is laid out by a Column and re-flows on resize.

#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include "nativeapi.h"

using namespace nativeapi;

int main() {
  if (!View::IsSupported()) {
    std::cerr << "Views are not supported on this platform." << std::endl;
    return 1;
  }

  auto& app = Application::GetInstance();

  auto window = std::make_shared<Window>();
  window->SetTitle("Sign in");
  window->SetContentSize(Size{360, 220});
  window->SetMinimumSize(Size{280, 180});
  window->Center();

  auto root = window->GetContentView();
  if (!root) {
    std::cerr << "The window has no content view." << std::endl;
    return 1;
  }
  root->SetLayout(ViewLayout::Column);
  root->SetPadding(EdgeInsets::All(16));
  root->SetSpacing(8);

  auto heading = std::make_shared<Label>("Welcome back");
  heading->SetFontSize(18);

  auto name = std::make_shared<TextField>();
  name->SetPlaceholder("Username");
  name->SetTooltip("Your account name");

  auto password = std::make_shared<TextField>();
  password->SetPlaceholder("Password");
  password->SetSecure(true);

  auto status = std::make_shared<Label>("Enter your credentials.");
  status->SetTextColor(Color::FromRGBA(128, 128, 128));

  // A row with a flexible spacer keeps the button at the trailing edge.
  auto actions = std::make_shared<View>();
  actions->SetLayout(ViewLayout::Row);
  actions->SetSpacing(8);
  actions->SetPreferredSize(Size{0, 32});

  auto spacer = std::make_shared<View>();
  spacer->SetFlex(1);

  auto clear = std::make_shared<Button>("Clear");
  auto sign_in = std::make_shared<Button>("Sign in");

  actions->AddSubview(spacer);
  actions->AddSubview(clear);
  actions->AddSubview(sign_in);

  // Every status change is echoed to stdout, so a GUI test can assert on it.
  auto set_status = [=](const std::string& text) {
    status->SetText(text);
    std::cout << "[view] status: " << text << std::endl;
  };
  auto submit = [=](const ViewEvent&) {
    if (name->GetText().empty()) {
      set_status("A username is required.");
      name->Focus();
      return;
    }
    set_status("Signing in as " + name->GetText() + "...");
  };
  sign_in->AddListener<ButtonClickedEvent>(submit);
  password->AddListener<TextFieldSubmittedEvent>(submit);
  name->AddListener<TextFieldSubmittedEvent>([=](const TextFieldSubmittedEvent&) {
    password->Focus();
  });
  name->AddListener<TextFieldChangedEvent>([](const TextFieldChangedEvent& event) {
    std::cout << "[view] username changed: " << event.GetText() << std::endl;
  });
  clear->AddListener<ButtonClickedEvent>([=](const ButtonClickedEvent&) {
    name->SetText("");
    password->SetText("");
    set_status("Enter your credentials.");
    name->Focus();
  });
  for (const auto& [label, view] : {std::pair<const char*, std::shared_ptr<View>>{"name", name},
                                    {"password", password},
                                    {"clear", clear},
                                    {"sign_in", sign_in}}) {
    view->AddListener<ViewFocusedEvent>(
        [label](const ViewFocusedEvent&) { std::cout << "[view] focused: " << label << std::endl; });
  }

  root->AddSubview(heading);
  root->AddSubview(name);
  root->AddSubview(password);
  root->AddSubview(status);
  root->AddSubview(actions);

  // "[view] layout <name> x y w h" per control, in the root's coordinates; printed
  // now and after every resize, so the re-flow can be checked from outside.
  auto print_layout = [=] {
    const Rectangle actions_frame = actions->GetFrame();
    auto print = [&](const char* label, const std::shared_ptr<View>& view, double dx, double dy) {
      const Rectangle f = view->GetFrame();
      std::cout << "[view] layout " << label << " " << f.x + dx << " " << f.y + dy << " "
                << f.width << " " << f.height << std::endl;
    };
    print("root", root, 0, 0);
    print("heading", heading, 0, 0);
    print("name", name, 0, 0);
    print("password", password, 0, 0);
    print("status", status, 0, 0);
    print("actions", actions, 0, 0);
    print("clear", clear, actions_frame.x, actions_frame.y);
    print("sign_in", sign_in, actions_frame.x, actions_frame.y);
  };
  print_layout();
  WindowManager::GetInstance().AddListener<WindowResizedEvent>(
      [=](const WindowResizedEvent& event) {
        if (event.GetWindowId() == window->GetId()) {
          print_layout();
        }
      });

  window->Show();
  name->Focus();

  WindowManager::GetInstance().AddListener<WindowClosedEvent>(
      [&app, window](const WindowClosedEvent& event) {
        if (event.GetWindowId() == window->GetId()) {
          app.Quit(0);
        }
      });
  return app.Run();
}

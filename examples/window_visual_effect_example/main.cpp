#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "nativeapi.h"

using nativeapi::Color;
using nativeapi::VisualEffect;
using nativeapi::Window;

// Walks a window through every VisualEffect, over a plain red window that gives the
// effects something to show. What the API reports is checked here - the exit code is the
// number of expectations that did not hold - and each step prints a "STEP" line once it is
// on screen, so that whoever watches (tools/gui has a test that does) knows when to look.
//
// It drives the windows itself and needs no input.
int main() {
  nativeapi::SetMainThread();

  int failures = 0;
  auto expect = [&](const std::string& what, bool ok) {
    std::cout << (ok ? "PASS " : "FAIL ") << what << std::endl;
    if (!ok) {
      failures++;
    }
  };

  const std::vector<std::pair<VisualEffect, std::string>> effects = {
      {VisualEffect::Blur, "Blur"},       {VisualEffect::Acrylic, "Acrylic"},
      {VisualEffect::Hud, "Hud"},         {VisualEffect::Popover, "Popover"},
      {VisualEffect::Menu, "Menu"},       {VisualEffect::Mica, "Mica"},
      {VisualEffect::MicaAlt, "MicaAlt"},
  };

  std::shared_ptr<Window> backdrop;
  std::shared_ptr<Window> window;
  VisualEffect in_force = VisualEffect::None;

  std::vector<std::function<void()>> steps = {[&] {
    backdrop = std::make_shared<Window>();
    backdrop->SetTitle("Backdrop");
    backdrop->SetBackgroundColor(Color::FromRGBA(255, 0, 0));
    backdrop->SetContentSize({560, 420});
    backdrop->SetPosition({200, 160});
    backdrop->Show();

    window = std::make_shared<Window>();
    window->SetTitle("Visual effect");
    window->SetContentSize({400, 260});
    window->SetPosition({280, 240});
    // A child stays above its parent, whichever of the two is brought forward.
    window->SetParentWindow(backdrop);
    window->Show();
    window->Focus();
    expect("a window starts without a visual effect",
           window->GetVisualEffect() == VisualEffect::None);
    expect("no effect is supported everywhere",
           Window::IsVisualEffectSupported(VisualEffect::None));
    std::cout << "STEP None applied" << std::endl;
  }};

  for (const auto& [effect, name] : effects) {
    steps.push_back([&, effect = effect, name = name] {
      const bool supported = Window::IsVisualEffectSupported(effect);
      const bool applied = window->SetVisualEffect(effect);
      expect(name + ": SetVisualEffect agrees with IsVisualEffectSupported", applied == supported);
      if (applied) {
        in_force = effect;
      }
      expect(name + ": GetVisualEffect is the effect in force",
             window->GetVisualEffect() == in_force);
      std::cout << "STEP " << name << (applied ? " applied" : " refused") << std::endl;
    });
  }

  steps.push_back([&] {
    // A background color given while an effect is active waits for the effect to go.
    window->SetBackgroundColor(Color::FromRGBA(0, 0, 255));
    expect("the background color is kept while an effect is active",
           window->GetBackgroundColor().b == 255 && window->GetBackgroundColor().r == 0);
    std::cout << "STEP Background " << (in_force == VisualEffect::None ? "shown" : "waiting")
              << std::endl;
  });
  steps.push_back([&] {
    expect("the effect can be removed", window->SetVisualEffect(VisualEffect::None));
    expect("no effect is in force afterwards", window->GetVisualEffect() == VisualEffect::None);
    std::cout << "STEP Removed applied" << std::endl;
  });
  steps.push_back([&] {
    std::cout << (failures == 0 ? "ALL PASS" : "FAILED") << std::endl;
    // Not Quit(): how a platform's loop ends must not decide the exit code.
    std::cout.flush();
    std::_Exit(failures);
  });

  std::thread driver([&] {
    for (size_t i = 0; i < steps.size(); i++) {
      // The windows stay as they first come up for a while: a watcher has to find them.
      std::this_thread::sleep_for(std::chrono::milliseconds(i == 1 ? 6000 : 2500));
      nativeapi::RunOnMainThread(steps[i]);
    }
  });
  driver.detach();

  // On Windows destroying any window of the library ends the loop (its window
  // procedure posts WM_QUIT), so enter it again: the last step ends the process.
  while (true) {
    nativeapi::Application::GetInstance().Run();
  }
}

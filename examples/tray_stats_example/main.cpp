// Tray stats example: a system monitor in the menu bar, laid out entirely with
// native views. The item reads like the status items next to it — a leading
// glyph, an optional role icon, the metrics, the network speed and a trailing
// history glyph — and the context menu switches between the layouts:
//
//   Horizontal   (◎) (chip)  CPU   MEM    2 KB/s  (history)
//                            44%   86%   86 KB/s
//
//   Stacked      (◎) (chip)  CPU 44%     2 KB/s  (history)
//                            MEM 86%    86 KB/s
//
// Metrics can be CPU and memory, temperature and power, fan and swap, CPU on
// its own, or none; the role icon and the network speed can be switched off.
// The numbers are simulated and change every second. Every column is as wide
// as its widest possible value, so the item keeps its width while they change
// and the neighbouring items do not jump.
//
// The flags set the starting layout, which is also how a GUI test or a
// screenshot reaches each one without opening the menu:
//
//   --stacked  --metrics=cpu-mem|temp-power|fan-swap|cpu|none
//   --no-role  --network  --full-load
//
// Every rebuild prints a "[tray_stats]" line with the layout and the item's
// bounds, so a GUI test can assert on it from outside.
//
// macOS shows the view. Windows and Linux only record it and keep showing the
// icon, which is what their notification areas can draw.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include "../../src/foundation/dispatcher.h"
#include "nativeapi.h"

using namespace nativeapi;

namespace {

// target: 18 x 18 pt, white on transparent, drawn for this example.
const char* kTargetIcon =
    "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";

// chip: 18 x 18 pt, white on transparent, drawn for this example.
const char* kChipIcon =
    "iVBORw0KGgoAAAANSUhEUgAAACQAAAAkCAYAAADhAJiYAAAEbElEQVR4nN1YSYtcVRT+TtdLOtrtEA0SFQdcOCzU"
    "hXvBjXGpggs3KupCd4Iu3DhjUFD8AzZuBIVoNoIY0IWgC1GiBBeCYBwDRrDRtEnb0yffq+90n6q8qq4iAdEDl/fq"
    "1L1nvt899wWmIJIzADQ2ImKj8Bu/rkcEzQsAPb1GxDr+18S+t3reRPIBktcnn2SP5N0k7yN5blmz13NvqzLOijHs"
    "j50kv2OfvpIh/n8ft+iJsu5N8zZIXm2e0j2WMvdjKUL2tMJ2AFgDMCu2/55X7UgfgPPLsnnPFZ0zaQC2tXiINuzE"
    "ZkHbmJ75a0P8xoZOXNQTRYj9/Ddlvp4zJWpJO0paki9jle6JnJ9kUihlEbEE4A/zFiNixVv/eInYcfHM/828kwCO"
    "mTddDXXshJAgkqqNJwFc4BRcRvIj15EikcruJ3mn115a0rWf5MsRcVSRssxNXYldA9S1LdnfXReR/IRnTj+SvK4r"
    "KiPTaUzZRXKW5Lx/v2iBp7yFcyuvlZG0Xnh6T1r2830pF15ZT6trICUlhDcDeAuAwC08lgBcDGBP4SXlVh9FvTKf"
    "Hn8B+N6pR6m9xyPioALQeKH+uBXADWMUVJLwFhgnpHTmPAA3dvyvujuoOU3BCBXpF46QSHk9AWAvgCtsRJTnOwC+"
    "zsO2rKHXPDy0q7RmEcA3AC40L9e9XRwdJJ3crp2d/n3I+a/1cmC7kJB8uqzLevrBR1BTRq9z22tHGW8SbTNyFRrS"
    "g8PeGSrI1SFbGvM+77CzEX5Ng0O3A5izYilUUbeRGqJZb4TV4kDKEK4oIl3nl/j7Slmk4x9GxEkFpQ2bhd4F4N0O"
    "IWt+9sr7uN2VVM+7mQKWH3TMfR7AM9JRi66e1FOfdxPSKFm764SslQPGnPmSshMA7gVwy5DHkzRbdU4eLccAvDLk"
    "vPS/ke9KVxv+iBBovTosleQd+VrYK94dSnnCwKaTfdZpxd7KiIjXRnoQsdmcj6ONDt6Vbty7+py2zkheXhyJMbIG"
    "qMntTlIF95JTBof4TylPBwrwPUhS4T8yBIypWLJe6EB0nZELrpmMuNbuj4gvWyjJKwzJR/nv0YJtaLLFhCE9cSd3"
    "37K9FqbMdKRmVHFnZGKIt2KZwjo45ZpzOOcMCCR5FYBdXixjVehPAXgIwN9G5hSejtQuYBR/xY5+BuCecjHQ/+ow"
    "vz3dpRH3JpLXuLk6U1rOO9rE5Oapjsb8a0m+R3LRzdnvJH8i+bONzUP3F/M0fvXcJZIflwtjM6xnaiOx9X7Eij8l"
    "OeeOTxfFVfMfK53ggnly4pI0Zjt9k1hHezJnJIc7yFMRkQWainaLZ77mwKi8x45ti0MThSv6V5j1crjqqZ4pG7Z6"
    "mrdX7zK3BdD6tWQcTXtwsp74BtQ00LZvfo5JvgyZ+EPDNAbRfUxjaBi+XnfJznH2rtKx9aFBWPKILwOH1EM5NerF"
    "n3OtvF6WPgvgKABhjC6I7aUT/zWKaSa75WgLOT/TlU93ovpJLz//KcoDbe44+gcwTEfhJlOWIgAAAABJRU5ErkJg"
    "gg==";

// history: 18 x 18 pt, white on transparent, drawn for this example.
const char* kHistoryIcon =
    "iVBORw0KGgoAAAANSUhEUgAAACQAAAAkCAYAAADhAJiYAAAELElEQVR4nO2YS4hcVRCG/5rp0fEVQyQQEt+PCIro"
    "6MIQUUEkcSEScSMICT4QH2QvgiKudONO3LkRRjeiYEAjoi7UleiIb1EXiW+cEA1xxnSmP6meqvHkzr3dt5NeuLDg"
    "0LdP1637n7+et6X/ZbCYjkOACUm+XJbMjNi32PfPnpn1jsf+SEBYBtNW34DJsTNEnNzMluL7xZK2S5qRtFnS+lA9"
    "JOkbSXOS3jKzuYJRkskTEgpGgOuBl4HDDJcl4G3gjuL+ybGAAU4FnhvycF9N8hpwThtQNgiMmfUAd8mspGskHZXU"
    "8UCWtDfWl5J+itvOknSJpG2SbpG0JnQdxM+S7jKzdxxUun9UZi4CfotTduNzFri6hY3zgWeAvytMbhvJfZEZvk4G"
    "Pi7ALAB3FnqdWBuAm2NdlfuF3lZgX9g5CvwOXBjPGJ6xjjyUXwojC7HyZFOR/v2HetAWDLxesTEV3zcHqIyz95sA"
    "1SHsxf4Xkg5Kmpa0y8zeBE4ys26l4Hlc9WL9lZue4q7roMzMS8FtkhYkdSVtlXRvxGinlduKk92Xp67oJEM7CoZe"
    "adBNph4JPQfybbDdD5E2oFaM1lE7IqC+e4A1wI8ByGV7Vb8xqDwtiVg50Z4UFdrLyJ+SXohy43tZNFcYGhjlZuYN"
    "0mNkoFp5PSBznBGL2pX3XRnXKzWpdaMcIB6kKUeCzboak73s6yL4vVad4fsZR+MAtDFO6OsyYJOZHamZDLKxHpA0"
    "H9deydfG9dgAdYIRf+Dlkj4Cdoa769La9cqu334C4N/smBzwm6fu00WmZQa9AVxa047WAwdD5wCwLu01PjzK/0ij"
    "AnAD8EnRIlz+AHYDp6U94NoC/NwqMAGg1nUsg9zkbcODr/Ykx9akU4AngcXK+HF2wVBZHGcbG623BeAC4FbgMeBV"
    "4PPoYx/G7xMtC+kM8F48eHfsTcVymyn9Zn1MnAEPA3uB7yujQjkybGk8yWpGky13/z3AdOGunQU7vwCn17nsxgoA"
    "93+3WIvu9zzlIEANY2+C2RBtI+eqx1exU9z0UCh5/SilG6f5ISbHpH5oMwy2puPae9gHhd3vYiyeqMuupHhPJUMy"
    "ffPT55mZ8r5MiGKoy0wtB7SNMf8QbPshryvZW0VvGPJMmI+YSRDPVjLmcGTJuhYMeRLcDeyPezM+H2h0VY2fby9Y"
    "+iz2POV/rbDnD3kKuAk4N9J9OuJkC/Ao8GlNbD7YyEwNqHTd82HAR1Mrhv13azIwWdsfKytwVb7yd7rWYEIxY2At"
    "8GIx5XUKnfsrdWSYeNw94ZV6qJtGfJWeyPEhjPrAv0PSFZLOk3RmqPrMvE+Su9sH/j1mdiiZafMuVtcGrOkdnBqj"
    "ntKS+m1F0qKZzVfviX9CWnX1WoaGgLLiL5eVv2JqQDT+PjKgUYRlgGlnPP9w/JfkH8jdT8btEVrmAAAAAElFTkSu"
    "QmCC";

enum class Style { Horizontal, Stacked };

enum class MetricSet { CpuMem, TempPower, FanSwap, CpuOnly, None };

const Color kBackground = Color::FromRGBA(63, 81, 181);
const Color kText = Color::FromRGBA(255, 255, 255);
const Color kCaption = Color::FromRGBA(255, 255, 255, 190);

constexpr double kCaptionSize = 9;
constexpr double kValueSize = 14;
constexpr double kStackedSize = 11;

// One simulated reading. `format` turns the value into its text; `widest` is
// the longest text it can produce, which sizes its column.
struct Metric {
  std::string caption;
  double value;
  double min;
  double max;
  double step;
  std::string (*format)(double);
  std::string widest;
};

std::string Format(const char* pattern, double value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), pattern, value);
  return buffer;
}

std::string Percent(double v) {
  return Format("%.0f%%", v);
}
std::string Celsius(double v) {
  return Format("%.0f°C", v);
}
std::string Watts(double v) {
  return Format("%.0fW", v);
}
std::string Rpm(double v) {
  return Format("%.1fk", v / 1000);
}
std::string Gigabytes(double v) {
  return Format("%.1f GB", v);
}

std::string Speed(double kilobytes) {
  if (kilobytes >= 1000) {
    return Format("%.0f MB/s", kilobytes / 1000);
  }
  return Format("%.0f KB/s", kilobytes);
}

// All the state, shared by the menu handlers, the rebuild and the ticker. Only
// touched on the main thread.
struct Monitor {
  Style style = Style::Horizontal;
  MetricSet metric_set = MetricSet::CpuMem;
  bool show_role = true;
  bool show_network = false;
  bool full_load = false;

  // Applies the command-line flags; false for one it does not know.
  bool Parse(const std::string& arg) {
    if (arg == "--stacked") {
      style = Style::Stacked;
    } else if (arg == "--no-role") {
      show_role = false;
    } else if (arg == "--network") {
      show_network = true;
    } else if (arg == "--full-load") {
      full_load = true;
    } else if (arg == "--metrics=cpu-mem") {
      metric_set = MetricSet::CpuMem;
    } else if (arg == "--metrics=temp-power") {
      metric_set = MetricSet::TempPower;
    } else if (arg == "--metrics=fan-swap") {
      metric_set = MetricSet::FanSwap;
    } else if (arg == "--metrics=cpu") {
      metric_set = MetricSet::CpuOnly;
    } else if (arg == "--metrics=none") {
      metric_set = MetricSet::None;
    } else {
      return false;
    }
    return true;
  }

  Metric cpu{"CPU", 44, 3, 100, 12, Percent, "100%"};
  Metric mem{"MEM", 86, 40, 100, 2, Percent, "100%"};
  Metric temp{"TMP", 58, 40, 99, 3, Celsius, "99°C"};
  Metric power{"PWR", 12, 2, 99, 4, Watts, "99W"};
  Metric fan{"FAN", 2100, 1200, 9900, 300, Rpm, "9.9k"};
  Metric swap{"SWP", 1.2, 0, 99, 0.2, Gigabytes, "99.9 GB"};
  double upload = 2;
  double download = 86;

  std::shared_ptr<TrayIcon> tray;
  std::shared_ptr<View> content;
  std::shared_ptr<Image> target_icon;
  std::shared_ptr<Image> role_icon;
  std::shared_ptr<Image> history_icon;

  // The labels the ticker rewrites, paired with what they show.
  std::vector<std::pair<std::shared_ptr<Label>, Metric*>> value_labels;
  std::shared_ptr<Label> upload_label;
  std::shared_ptr<Label> download_label;

  std::mt19937 random{std::random_device{}()};

  std::vector<Metric*> Metrics() {
    switch (metric_set) {
      case MetricSet::CpuMem:
        return {&cpu, &mem};
      case MetricSet::TempPower:
        return {&temp, &power};
      case MetricSet::FanSwap:
        return {&fan, &swap};
      case MetricSet::CpuOnly:
        return {&cpu};
      case MetricSet::None:
        return {};
    }
    return {};
  }

  // A stacked line reads "CPU 44%": caption and value in one label.
  std::string Text(const Metric& metric) const {
    return style == Style::Stacked ? metric.caption + " " + metric.format(metric.value)
                                   : metric.format(metric.value);
  }
};

std::shared_ptr<Label> MakeLabel(const std::string& text, double size, Color color) {
  auto label = std::make_shared<Label>(text);
  label->SetFontSize(size);
  label->SetTextColor(color);
  return label;
}

// How wide `text` is at `size`: measured by a label that is never shown.
double TextWidth(const std::string& text, double size) {
  return std::ceil(MakeLabel(text, size, kText)->GetIntrinsicSize().width);
}

std::shared_ptr<View> MakeIcon(const std::shared_ptr<Image>& image) {
  auto icon = std::make_shared<ImageView>();
  icon->SetImage(image);
  icon->SetPreferredSize(Size{16, 16});
  icon->SetAlignment(ViewAlignment::Center);
  return icon;
}

// A column of lines, left or right aligned, centred in the row.
std::shared_ptr<View> MakeColumn(double width) {
  auto column = std::make_shared<View>();
  column->SetLayout(ViewLayout::Column);
  column->SetAlignment(ViewAlignment::Center);
  column->SetPreferredSize(Size{width, 0});
  return column;
}

void Rebuild(Monitor& m) {
  auto& content = *m.content;
  content.ClearSubviews();
  m.value_labels.clear();
  m.upload_label = nullptr;
  m.download_label = nullptr;

  content.AddSubview(MakeIcon(m.target_icon));
  if (m.show_role) {
    content.AddSubview(MakeIcon(m.role_icon));
  }

  const auto metrics = m.Metrics();
  if (!metrics.empty() && m.style == Style::Horizontal) {
    // One column per metric: a small caption over the value.
    for (Metric* metric : metrics) {
      const double width = std::max(TextWidth(metric->widest, kValueSize),
                                    TextWidth(metric->caption, kCaptionSize));
      auto column = MakeColumn(width);
      column->AddSubview(MakeLabel(metric->caption, kCaptionSize, kCaption));
      auto value = MakeLabel(m.Text(*metric), kValueSize, kText);
      column->AddSubview(value);
      m.value_labels.emplace_back(value, metric);
      content.AddSubview(column);
    }
  } else if (!metrics.empty()) {
    // One column for all of them, a metric per line.
    double width = 0;
    for (Metric* metric : metrics) {
      width = std::max(width, TextWidth(metric->caption + " " + metric->widest, kStackedSize));
    }
    auto column = MakeColumn(width);
    for (Metric* metric : metrics) {
      auto line = MakeLabel(m.Text(*metric), kStackedSize, kText);
      column->AddSubview(line);
      m.value_labels.emplace_back(line, metric);
    }
    content.AddSubview(column);
  }

  if (m.show_network) {
    // Upload over download, right-aligned so the units line up.
    auto column = MakeColumn(TextWidth("999 KB/s", kStackedSize));
    m.upload_label = MakeLabel(Speed(m.upload), kStackedSize, kText);
    m.download_label = MakeLabel(Speed(m.download), kStackedSize, kText);
    m.upload_label->SetTextAlignment(TextAlignment::End);
    m.download_label->SetTextAlignment(TextAlignment::End);
    column->AddSubview(m.upload_label);
    column->AddSubview(m.download_label);
    content.AddSubview(column);
  }

  content.AddSubview(MakeIcon(m.history_icon));
}

void PrintBounds(Monitor& m, const std::string& what) {
  const Rectangle b = m.tray->GetBounds();
  std::cout << "[tray_stats] " << what << " bounds " << b.x << " " << b.y << " " << b.width
            << " " << b.height << std::endl;
}

// Moves every reading a random step, or pins them at the top under full load.
void Tick(Monitor& m) {
  for (Metric* metric : {&m.cpu, &m.mem, &m.temp, &m.power, &m.fan, &m.swap}) {
    if (m.full_load) {
      metric->value = metric->max;
      continue;
    }
    std::uniform_real_distribution<double> step(-metric->step, metric->step);
    metric->value = std::clamp(metric->value + step(m.random), metric->min, metric->max);
  }
  std::uniform_real_distribution<double> factor(0.5, 1.6);
  m.upload = m.full_load ? 860 : std::clamp(m.upload * factor(m.random), 1.0, 999.0);
  m.download = m.full_load ? 12000 : std::clamp(m.download * factor(m.random), 1.0, 999.0);

  for (auto& [label, metric] : m.value_labels) {
    label->SetText(m.Text(*metric));
  }
  if (m.upload_label) {
    m.upload_label->SetText(Speed(m.upload));
    m.download_label->SetText(Speed(m.download));
  }
}

std::shared_ptr<MenuItem> AddItem(Menu& menu,
                                  const std::string& label,
                                  MenuItemType type,
                                  bool checked,
                                  std::function<void()> action) {
  auto item = std::make_shared<MenuItem>(label, type);
  if (checked) {
    item->SetState(MenuItemState::Checked);
  }
  item->AddListener<MenuItemClickedEvent>([action](const MenuItemClickedEvent&) { action(); });
  menu.AddItem(item);
  return item;
}

}  // namespace

int main(int argc, char** argv) {
  if (!TrayManager::GetInstance().IsSupported() || !View::IsSupported()) {
    std::cerr << "Tray icons or views are not supported on this platform." << std::endl;
    return 1;
  }

  auto& app = Application::GetInstance();
  auto m = std::make_shared<Monitor>();
  for (int i = 1; i < argc; ++i) {
    if (!m->Parse(argv[i])) {
      std::cerr << "Unknown option: " << argv[i] << std::endl;
      return 1;
    }
  }

  m->target_icon = Image::FromBase64(kTargetIcon);
  m->role_icon = Image::FromBase64(kChipIcon);
  m->history_icon = Image::FromBase64(kHistoryIcon);

  m->tray = std::make_shared<TrayIcon>();
  // What Windows and Linux show, and macOS without a content view.
  m->tray->SetIcon(m->target_icon);
  m->tray->SetIconTemplate(true);
  m->tray->SetTooltip("nativeapi tray stats example");

  m->content = std::make_shared<View>();
  m->content->SetLayout(ViewLayout::Row);
  m->content->SetPadding(EdgeInsets{0, 10, 0, 10});
  m->content->SetSpacing(12);
  m->content->SetBackgroundColor(kBackground);

  // --- Context menu: one radio group per choice, a checkbox per toggle ---
  auto menu = std::make_shared<Menu>();
  // Every item that changes the layout rebuilds the view and reports it.
  auto apply = [m](const std::string& what) {
    Rebuild(*m);
    PrintBounds(*m, what);
  };
  auto radio = [&](const std::string& label, int group, bool checked, void (*set)(Monitor&)) {
    auto item = std::make_shared<MenuItem>(label, MenuItemType::Radio);
    item->SetRadioGroup(group);
    if (checked) {
      item->SetState(MenuItemState::Checked);
    }
    std::weak_ptr<MenuItem> weak = item;
    item->AddListener<MenuItemClickedEvent>([m, set, apply, label, weak](const auto&) {
      set(*m);
      if (auto self = weak.lock()) {
        self->SetState(MenuItemState::Checked);  // unchecks the rest of the group
      }
      apply(label);
    });
    menu->AddItem(item);
  };
  auto toggle = [&](const std::string& label, bool checked, bool Monitor::*flag) {
    auto item = std::make_shared<MenuItem>(label, MenuItemType::Checkbox);
    item->SetState(checked ? MenuItemState::Checked : MenuItemState::Unchecked);
    std::weak_ptr<MenuItem> weak = item;
    item->AddListener<MenuItemClickedEvent>([m, flag, apply, label, weak](const auto&) {
      (*m).*flag = !((*m).*flag);
      if (auto self = weak.lock()) {
        self->SetState((*m).*flag ? MenuItemState::Checked : MenuItemState::Unchecked);
      }
      Tick(*m);
      apply(label + ((*m).*flag ? " on" : " off"));
    });
    menu->AddItem(item);
  };

  const Style style = m->style;
  const MetricSet set = m->metric_set;
  radio("Horizontal", 1, style == Style::Horizontal, [](Monitor& m) { m.style = Style::Horizontal; });
  radio("Stacked", 1, style == Style::Stacked, [](Monitor& m) { m.style = Style::Stacked; });
  menu->AddSeparator();
  radio("CPU and Memory", 2, set == MetricSet::CpuMem, [](Monitor& m) { m.metric_set = MetricSet::CpuMem; });
  radio("Temperature and Power", 2, set == MetricSet::TempPower,
        [](Monitor& m) { m.metric_set = MetricSet::TempPower; });
  radio("Fan and Swap", 2, set == MetricSet::FanSwap, [](Monitor& m) { m.metric_set = MetricSet::FanSwap; });
  radio("CPU Only", 2, set == MetricSet::CpuOnly, [](Monitor& m) { m.metric_set = MetricSet::CpuOnly; });
  radio("No Metrics", 2, set == MetricSet::None, [](Monitor& m) { m.metric_set = MetricSet::None; });
  menu->AddSeparator();
  toggle("Show Role Icon", m->show_role, &Monitor::show_role);
  toggle("Show Network Speed", m->show_network, &Monitor::show_network);
  toggle("Simulate Full Load", m->full_load, &Monitor::full_load);
  menu->AddSeparator();
  AddItem(*menu, "Quit", MenuItemType::Normal, false, [&app] { app.Quit(0); });

  m->tray->SetContextMenu(menu);
  m->tray->SetContextMenuTrigger(ContextMenuTrigger::RightClicked);
  m->tray->AddListener<TrayIconClickedEvent>(
      [m](const TrayIconClickedEvent&) { PrintBounds(*m, "tray icon clicked"); });

  Tick(*m);
  Rebuild(*m);
  m->tray->SetContentView(m->content);
  m->tray->SetVisible(true);

  // Views are main-thread only: the ticker thread posts each update there.
  // The first tick also reports where the item landed; right after
  // SetVisible() the menu bar has not placed it yet.
  std::atomic<bool> running{true};
  std::thread ticker([&running, m] {
    bool first = true;
    while (running) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      RunOnMainThread([m, first] {
        Tick(*m);
        if (first) {
          PrintBounds(*m, "started");
        }
      });
      first = false;
    }
  });

  std::cout << "Tray stats example running. Right-click the item for the menu." << std::endl;
  const int exit_code = app.Run();

  running = false;
  ticker.join();
  return exit_code;
}

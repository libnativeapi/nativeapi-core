// The Windows screen coordinate space (src/platform/windows/screen_layout_windows.h),
// with monitor layouts that cannot be set up on a test machine.
#include "../src/platform/windows/screen_layout_windows.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace nativeapi;
using namespace nativeapi::screen_layout;

static int failures = 0;

static void Check(bool ok, const std::string& what) {
  if (!ok) {
    std::printf("FAIL %s\n", what.c_str());
    ++failures;
  }
}

static bool Near(double a, double b) {
  return std::fabs(a - b) < 1e-6;
}

static Monitor M(long left, long top, long width, long height, double scale, bool primary = false) {
  return {{left, top, left + width, top + height}, scale, primary, {0, 0, 0, 0}};
}

static std::vector<Monitor> Arranged(std::vector<Monitor> monitors) {
  Arrange(monitors);
  return monitors;
}

static void CheckLogical(const Monitor& m, double x, double y, double width, double height,
                         const std::string& name) {
  Check(Near(m.logical.x, x) && Near(m.logical.y, y) && Near(m.logical.width, width) &&
            Near(m.logical.height, height),
        name + ": logical " + std::to_string(m.logical.x) + "," + std::to_string(m.logical.y) +
            " " + std::to_string(m.logical.width) + "x" + std::to_string(m.logical.height));
}

// Every physical pixel sampled on every monitor survives the trip through
// logical space, and touching monitors neither overlap nor part.
static void CheckSpace(const std::vector<Monitor>& monitors, const std::string& name) {
  for (const auto& m : monitors) {
    for (long y = m.physical.top; y < m.physical.bottom; y += 97) {
      for (long x = m.physical.left; x < m.physical.right; x += 89) {
        const Point logical = PhysicalToLogical(monitors, x, y);
        long px = 0, py = 0;
        LogicalToPhysical(monitors, logical, &px, &py);
        if (px != x || py != y) {
          Check(false, name + ": round trip of " + std::to_string(x) + "," + std::to_string(y) +
                           " came back as " + std::to_string(px) + "," + std::to_string(py));
          return;
        }
      }
    }
  }
  for (size_t i = 0; i < monitors.size(); ++i) {
    for (size_t j = i + 1; j < monitors.size(); ++j) {
      const Rectangle& a = monitors[i].logical;
      const Rectangle& b = monitors[j].logical;
      const double overlap_x = std::fmin(a.x + a.width, b.x + b.width) - std::fmax(a.x, b.x);
      const double overlap_y = std::fmin(a.y + a.height, b.y + b.height) - std::fmax(a.y, b.y);
      Check(!(overlap_x > 1e-6 && overlap_y > 1e-6),
            name + ": monitors " + std::to_string(i) + " and " + std::to_string(j) + " overlap");
    }
  }
}

int main() {
  {
    auto m = Arranged({M(0, 0, 2560, 1440, 1.5, true)});
    CheckLogical(m[0], 0, 0, 2560 / 1.5, 960, "single");
    CheckSpace(m, "single");
  }
  {
    // One factor everywhere: the physical space divided by it, as before.
    auto m = Arranged({M(0, 0, 2560, 1440, 1.5, true), M(2560, -300, 2560, 1440, 1.5)});
    CheckLogical(m[1], 2560 / 1.5, -200, 2560 / 1.5, 960, "same factor");
    CheckSpace(m, "same factor");
  }
  {
    // 150 % primary, 100 % on its right: no gap between them.
    auto m = Arranged({M(0, 0, 2560, 1440, 1.5, true), M(2560, 0, 1920, 1080, 1.0)});
    CheckLogical(m[1], 2560 / 1.5, 0, 1920, 1080, "150 % | 100 %");
    const Point p = PhysicalToLogical(m, 2600, 100);
    Check(Near(p.x, 2560 / 1.5 + 40) && Near(p.y, 100), "150 % | 100 %: point on the right monitor");
    CheckSpace(m, "150 % | 100 %");
  }
  {
    // 150 % primary, 200 % on its right. Dividing by each monitor's own factor
    // put the second one at 960, over the first (0 to 1280).
    auto m = Arranged({M(0, 0, 1920, 1080, 1.5, true), M(1920, 0, 3840, 2160, 2.0)});
    CheckLogical(m[1], 1280, 0, 1920, 1080, "150 % | 200 %");
    CheckSpace(m, "150 % | 200 %");
  }
  {
    auto m = Arranged({M(0, 0, 2560, 1440, 1.5, true), M(-1920, 200, 1920, 1080, 1.0)});
    CheckLogical(m[1], -1920, 200 / 1.5, 1920, 1080, "left, lower");
    CheckSpace(m, "left, lower");
  }
  {
    auto m = Arranged({M(0, 0, 2560, 1440, 1.5, true), M(500, 1440, 1920, 1080, 1.0)});
    CheckLogical(m[1], 500 / 1.5, 960, 1920, 1080, "below, shifted");
    CheckSpace(m, "below, shifted");
  }
  {
    auto m = Arranged({M(0, 0, 1920, 1080, 1.25, true), M(0, -2160, 3840, 2160, 2.0)});
    CheckLogical(m[1], 0, -1080, 1920, 1080, "above");
    CheckSpace(m, "above");
  }
  {
    // A chain: the third monitor is placed against the second.
    auto m = Arranged({M(1920, 0, 3840, 2160, 2.0), M(0, 0, 1920, 1080, 1.0, true),
                       M(5760, 0, 1920, 1080, 1.0)});
    CheckLogical(m[1], 0, 0, 1920, 1080, "chain: primary");
    CheckLogical(m[0], 1920, 0, 1920, 1080, "chain: middle");
    CheckLogical(m[2], 3840, 0, 1920, 1080, "chain: last");
    CheckSpace(m, "chain");
  }
  {
    // Touching none: its physical origin over its own factor.
    auto m = Arranged({M(0, 0, 1920, 1080, 1.0, true), M(4000, 0, 2560, 1440, 2.0)});
    CheckLogical(m[1], 2000, 0, 1280, 720, "apart");
    CheckSpace(m, "apart");
  }
  {
    // Points on no monitor go through the nearest one.
    auto m = Arranged({M(0, 0, 2560, 1440, 1.5, true), M(2560, 0, 1920, 1080, 1.0)});
    const Point below_right = PhysicalToLogical(m, 3000, 1300);
    Check(Near(below_right.x, 2560 / 1.5 + 440) && Near(below_right.y, 1300),
          "off screen: nearest monitor");
    long x = 0, y = 0;
    LogicalToPhysical(m, {-100, -50}, &x, &y);
    Check(x == -150 && y == -75, "off screen: primary's factor to its top left");
  }
  std::printf("%s\n", failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}

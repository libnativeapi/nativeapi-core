#pragma once

// The library's screen coordinate space on Windows, as plain arithmetic so that
// it can be tested anywhere (tests/screen_layout_test.cpp). No <windows.h>.
//
// Windows positions windows and monitors in physical pixels, and every monitor
// has a scale factor of its own. The library reports logical pixels. Dividing a
// position by the scale factor of whatever monitor it is on does not make one
// space: with monitors at different factors the monitors overlap or leave gaps
// in it, a logical point can stand for two physical ones, and a window saved on
// one monitor comes back in the wrong place. So, as Chromium does:
//
// - Every monitor gets a logical rectangle: its physical size divided by its
//   own scale factor, placed so that monitors that touch physically touch in
//   logical space too. The primary monitor is the root; a monitor touching one
//   already placed is placed against that edge, its offset along the edge
//   scaled by the placed monitor's factor. A monitor touching none keeps its
//   physical origin divided by its own factor.
// - Inside a monitor, logical and physical pixels differ by that monitor's
//   factor only. A point is converted with the monitor it lies on; a point on
//   no monitor with the nearest one.
//
// With every monitor at the same factor this is the physical space divided by
// that factor, as it always was.

#include <cmath>
#include <cstddef>
#include <vector>

#include "../../foundation/geometry.h"

namespace nativeapi {
namespace screen_layout {

struct PixelRect {
  long left;
  long top;
  long right;
  long bottom;
};

struct Monitor {
  PixelRect physical;
  double scale;
  bool primary;
  // Filled in by Arrange().
  Rectangle logical;
};

inline double SafeScale(double scale) {
  return scale > 0.0 ? scale : 1.0;
}

// Places `child` against `parent` when their edges touch; false when they
// don't. Corners alone do not count.
inline bool PlaceAgainst(const Monitor& parent, Monitor& child) {
  const PixelRect& p = parent.physical;
  const PixelRect& c = child.physical;
  const double ps = SafeScale(parent.scale);
  const double cs = SafeScale(child.scale);
  const double child_width = (c.right - c.left) / cs;
  const double child_height = (c.bottom - c.top) / cs;
  const bool rows_overlap = c.top < p.bottom && c.bottom > p.top;
  const bool columns_overlap = c.left < p.right && c.right > p.left;
  double x = 0, y = 0;
  if (rows_overlap && c.left == p.right) {
    x = parent.logical.x + parent.logical.width;
    y = parent.logical.y + (c.top - p.top) / ps;
  } else if (rows_overlap && c.right == p.left) {
    x = parent.logical.x - child_width;
    y = parent.logical.y + (c.top - p.top) / ps;
  } else if (columns_overlap && c.top == p.bottom) {
    x = parent.logical.x + (c.left - p.left) / ps;
    y = parent.logical.y + parent.logical.height;
  } else if (columns_overlap && c.bottom == p.top) {
    x = parent.logical.x + (c.left - p.left) / ps;
    y = parent.logical.y - child_height;
  } else {
    return false;
  }
  child.logical = {x, y, child_width, child_height};
  return true;
}

// Fills in every monitor's logical rectangle.
inline void Arrange(std::vector<Monitor>& monitors) {
  std::vector<bool> placed(monitors.size(), false);
  auto place_alone = [&](size_t i) {
    Monitor& m = monitors[i];
    const double s = SafeScale(m.scale);
    m.logical = {m.physical.left / s, m.physical.top / s, (m.physical.right - m.physical.left) / s,
                 (m.physical.bottom - m.physical.top) / s};
    placed[i] = true;
  };
  for (size_t i = 0; i < monitors.size(); ++i) {
    if (monitors[i].primary) {
      place_alone(i);
      break;
    }
  }
  for (;;) {
    bool progress = false;
    for (size_t i = 0; i < monitors.size(); ++i) {
      if (placed[i])
        continue;
      for (size_t j = 0; j < monitors.size() && !placed[i]; ++j) {
        if (placed[j] && PlaceAgainst(monitors[j], monitors[i])) {
          placed[i] = true;
          progress = true;
        }
      }
    }
    if (progress)
      continue;
    // Nothing touches what is placed: start again from the first monitor left.
    size_t next = monitors.size();
    for (size_t i = 0; i < monitors.size(); ++i) {
      if (!placed[i]) {
        next = i;
        break;
      }
    }
    if (next == monitors.size())
      break;
    place_alone(next);
  }
}

inline double DistanceSquared(double x, double y, double left, double top, double right,
                              double bottom) {
  const double dx = x < left ? left - x : (x >= right ? x - right : 0);
  const double dy = y < top ? top - y : (y >= bottom ? y - bottom : 0);
  return dx * dx + dy * dy;
}

// The monitor a physical point lies on, or the nearest one; nullptr only when
// there are none.
inline const Monitor* MonitorAtPhysical(const std::vector<Monitor>& monitors, long x, long y) {
  for (const auto& m : monitors) {
    if (x >= m.physical.left && x < m.physical.right && y >= m.physical.top &&
        y < m.physical.bottom)
      return &m;
  }
  const Monitor* best = nullptr;
  double best_distance = 0;
  for (const auto& m : monitors) {
    const double d = DistanceSquared(x, y, m.physical.left, m.physical.top, m.physical.right,
                                     m.physical.bottom);
    if (!best || d < best_distance) {
      best = &m;
      best_distance = d;
    }
  }
  return best;
}

// The monitor a logical point lies on, or the nearest one.
inline const Monitor* MonitorAtLogical(const std::vector<Monitor>& monitors, Point point) {
  for (const auto& m : monitors) {
    if (point.x >= m.logical.x && point.x < m.logical.x + m.logical.width &&
        point.y >= m.logical.y && point.y < m.logical.y + m.logical.height)
      return &m;
  }
  const Monitor* best = nullptr;
  double best_distance = 0;
  for (const auto& m : monitors) {
    const double d = DistanceSquared(point.x, point.y, m.logical.x, m.logical.y,
                                     m.logical.x + m.logical.width, m.logical.y + m.logical.height);
    if (!best || d < best_distance) {
      best = &m;
      best_distance = d;
    }
  }
  return best;
}

inline Point ToLogical(const Monitor& m, double x, double y) {
  const double s = SafeScale(m.scale);
  return {m.logical.x + (x - m.physical.left) / s, m.logical.y + (y - m.physical.top) / s};
}

inline void ToPhysical(const Monitor& m, Point point, long* x, long* y) {
  const double s = SafeScale(m.scale);
  *x = m.physical.left + std::lround((point.x - m.logical.x) * s);
  *y = m.physical.top + std::lround((point.y - m.logical.y) * s);
}

inline Point PhysicalToLogical(const std::vector<Monitor>& monitors, long x, long y) {
  const Monitor* m = MonitorAtPhysical(monitors, x, y);
  return m ? ToLogical(*m, x, y) : Point{static_cast<double>(x), static_cast<double>(y)};
}

inline void LogicalToPhysical(const std::vector<Monitor>& monitors, Point point, long* x, long* y) {
  const Monitor* m = MonitorAtLogical(monitors, point);
  if (m) {
    ToPhysical(*m, point, x, y);
  } else {
    *x = std::lround(point.x);
    *y = std::lround(point.y);
  }
}

}  // namespace screen_layout
}  // namespace nativeapi

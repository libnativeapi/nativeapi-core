#pragma once
#include <windows.h>
#include "../../window.h"
namespace nativeapi {
bool SetWinUI3TitleBarStyle(HWND window, TitleBarStyle style);
bool SetWinUI3Frameless(HWND window);
bool SetWinUI3TitleBarColors(HWND window, const Color& background, const Color& foreground);
bool ResetWinUI3TitleBarColors(HWND window);
}

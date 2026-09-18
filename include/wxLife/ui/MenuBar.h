#pragma once

#include <wx/menu.h>

namespace wxLife::ui
{

/// Builds the menus and accelerators. The frame takes ownership with SetMenuBar().
[[nodiscard]] wxMenuBar* buildMenuBar();

}  // namespace wxLife::ui

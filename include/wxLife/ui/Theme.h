#pragma once

#include <wx/event.h>
#include <wx/settings.h>
#include <wx/window.h>

#include "wxLife/render/RenderStyle.h"
#include "wxLife/ui/Defaults.h"
#include "wxLife/ui/WxConvert.h"

// Colours follow the desktop's light or dark theme. When the theme changes, wx sends
// wxEVT_SYS_COLOUR_CHANGED to every window; handlers call Skip() so wx passes it on to the
// child windows.

namespace wxLife::ui
{

/// Canvas colours for the current theme.
[[nodiscard]] inline render::RenderStyle themeStyle()
{
    return wxSystemSettings::GetAppearance().IsDark() ? render::darkStyle() : render::lightStyle();
}

/// Shows `text` in the error colour of the current theme, now and after every theme change.
inline void useErrorColour(wxWindow& text)
{
    const auto apply = [&text] {
        const bool dark = wxSystemSettings::GetAppearance().IsDark();
        text.SetForegroundColour(
            toWx(dark ? defaults::kErrorTextOnDark : defaults::kErrorTextOnLight));
    };
    apply();
    text.Bind(wxEVT_SYS_COLOUR_CHANGED, [apply](wxSysColourChangedEvent& event) {
        apply();
        event.Skip();
    });
}

}  // namespace wxLife::ui

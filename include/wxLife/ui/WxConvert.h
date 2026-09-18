#pragma once

#include <string>
#include <string_view>

#include <wx/colour.h>
#include <wx/string.h>

#include "wxLife/render/Types.h"

namespace wxLife::ui
{

/// UTF-8 → wxString. wxGTK keeps the C locale, so implicit std::string conversions would mangle
/// text such as "×".
[[nodiscard]] inline wxString toWx(std::string_view utf8)
{
    return wxString::FromUTF8(utf8.data(), utf8.size());
}

[[nodiscard]] inline std::string toUtf8(const wxString& text)
{
    return text.utf8_string();
}

[[nodiscard]] inline wxColour toWx(render::Rgb c)
{
    return {c.r, c.g, c.b};
}

}  // namespace wxLife::ui

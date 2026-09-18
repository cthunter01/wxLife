#include "wxLife/ui/WorldSizeDialog.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include <wx/arrstr.h>
#include <wx/sizer.h>

#include "wxLife/core/Format.h"
#include "wxLife/core/Types.h"
#include "wxLife/core/WorldLimits.h"
#include "wxLife/ui/Theme.h"
#include "wxLife/ui/WxConvert.h"

namespace wxLife::ui
{

namespace
{

// Square presets; the choice lists "Choose…" first and "Fit window" last.
constexpr std::array<core::Coord, 8> kSquareSides{100,   256,   500,    1'000,
                                                  2'000, 5'000, 10'000, 20'000};

constexpr int kChooseIndex      = 0;
constexpr int kFirstSquareIndex = 1;
constexpr int kFitWindowIndex   = kFirstSquareIndex + static_cast<int>(kSquareSides.size());

constexpr std::string_view kNotWholeNumbers = "Width and height must be whole numbers.";

[[nodiscard]] std::string sizeText(core::Extent extent)
{
    return std::format("{} × {}", core::formatCount(static_cast<std::uint64_t>(extent.width)),
                       core::formatCount(static_cast<std::uint64_t>(extent.height)));
}

[[nodiscard]] std::string memoryText(std::string_view bytes, std::uint64_t budget)
{
    return std::format("Memory: {} of {} budget", bytes, core::formatBytes(budget));
}

[[nodiscard]] wxSpinCtrl* makeSideSpin(wxWindow* parent, core::Coord value)
{
    return new wxSpinCtrl(parent, wxID_ANY, wxString(), wxDefaultPosition, wxDefaultSize,
                          wxSP_ARROW_KEYS, core::kMinWorldSide, core::kMaxWorldSide, value);
}

// The number typed into a side box, or nullopt if the text is not a whole number.
// wxSpinCtrl::GetValue() would silently clamp it to the spin range instead. Numbers beyond the
// limits stay beyond them, so validateExtent() rejects them.
[[nodiscard]] std::optional<core::Coord> typedSide(const wxSpinCtrl& spin)
{
    const std::string text   = toUtf8(spin.GetTextValue().Strip(wxString::both));
    std::string_view  digits = text;
    // GTK's box accepts a leading '+'; from_chars does not.
    if (digits.starts_with('+') && !digits.starts_with("+-"))
    {
        digits.remove_prefix(1);
    }
    const char* const first  = std::to_address(digits.begin());
    const char* const last   = std::to_address(digits.end());
    std::int64_t      value  = 0;
    const auto [stop, error] = std::from_chars(first, last, value);
    if (stop != last || (error != std::errc{} && error != std::errc::result_out_of_range))
    {
        return std::nullopt;
    }
    if (error == std::errc::result_out_of_range)  // more digits than int64_t holds
    {
        value = digits.starts_with('-') ? std::numeric_limits<std::int64_t>::min()
                                        : std::numeric_limits<std::int64_t>::max();
    }
    return static_cast<core::Coord>(
        std::clamp<std::int64_t>(value, core::kMinWorldSide - 1, core::kMaxWorldSide + 1));
}

}  // namespace

std::optional<WorldSizeRequest> WorldSizeDialog::ask(wxWindow* parent, core::Extent current,
                                                     core::Extent  fitsCanvas,
                                                     std::uint64_t memoryBudgetBytes)
{
    WorldSizeDialog dialog(parent, current, fitsCanvas, memoryBudgetBytes);
    if (dialog.ShowModal() != wxID_OK)
    {
        return std::nullopt;
    }
    return dialog.request();
}

WorldSizeDialog::WorldSizeDialog(wxWindow* parent, core::Extent current, core::Extent fitsCanvas,
                                 std::uint64_t memoryBudgetBytes)
  : wxDialog(parent, wxID_ANY, "World Size"),
    fitsCanvas_(fitsCanvas),
    budget_(memoryBudgetBytes),
    width_(makeSideSpin(this, current.width)),
    height_(makeSideSpin(this, current.height))
{
    wxArrayString presetNames;
    presetNames.Add(toWx("Choose…"));
    for (const core::Coord side : kSquareSides)
    {
        presetNames.Add(toWx(sizeText({.width = side, .height = side})));
    }
    presetNames.Add(toWx(std::format("Fit window ({})", sizeText(fitsCanvas_))));
    presets_ = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, presetNames);
    presets_->SetSelection(kChooseIndex);

    keepPattern_ = new wxCheckBox(this, wxID_ANY, "Keep the current pattern (centred)");
    keepPattern_->SetValue(true);

    // Start with the widest texts revalidate() can write, so the fitted dialog has room for them.
    const core::Extent largest{.width = core::kMaxWorldSide, .height = core::kMaxWorldSide};
    memory_ = new wxStaticText(
        this, wxID_ANY, toWx(memoryText(core::formatBytes(core::worldBytes(largest)), budget_)));
    const std::array<std::string, 4> errors{
        std::string(kNotWholeNumbers),
        core::describe(core::ExtentError::TooSmall, largest, budget_),
        core::describe(core::ExtentError::TooLarge, largest, budget_),
        core::describe(core::ExtentError::OverMemoryBudget, largest, budget_)};
    const auto textWidth = [this](const std::string& text) { return GetTextExtent(toWx(text)).x; };
    error_ = new wxStaticText(this, wxID_ANY, toWx(std::ranges::max(errors, {}, textWidth)));
    useErrorColour(*error_);

    const int  gap      = wxSizerFlags::GetDefaultBorder();
    auto*      fields   = new wxFlexGridSizer(3, gap, 2 * gap);  // label | control | unit
    const auto addField = [&](const wxString& label, wxWindow* control, const wxString& unit) {
        fields->Add(new wxStaticText(this, wxID_ANY, label), wxSizerFlags().CentreVertical());
        fields->Add(control, wxSizerFlags().Expand());
        fields->Add(new wxStaticText(this, wxID_ANY, unit), wxSizerFlags().CentreVertical());
    };
    addField("Width", width_, "cells");
    addField("Height", height_, "cells");
    addField("Preset", presets_, wxString());

    wxStdDialogButtonSizer* buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);

    ok_ = buttons->GetAffirmativeButton();

    auto* column = new wxBoxSizer(wxVERTICAL);
    column->Add(fields, wxSizerFlags().Expand().DoubleBorder());
    column->Add(keepPattern_, wxSizerFlags().DoubleBorder(wxLEFT | wxRIGHT | wxBOTTOM));
    column->Add(memory_, wxSizerFlags().DoubleBorder(wxLEFT | wxRIGHT));
    column->Add(error_, wxSizerFlags().DoubleBorder(wxLEFT | wxRIGHT | wxBOTTOM));
    column->Add(buttons, wxSizerFlags().Expand().DoubleBorder(wxLEFT | wxRIGHT | wxBOTTOM));
    SetSizerAndFit(column);
    CentreOnParent();

    // Typing sends wxEVT_TEXT but no spin event, so both are needed for a live check.
    for (wxSpinCtrl* spin : {width_, height_})
    {
        spin->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { revalidate(); });
        spin->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { revalidate(); });
    }
    presets_->Bind(wxEVT_CHOICE,
                   [this](wxCommandEvent& event) { applyPreset(event.GetSelection()); });
    revalidate();
}

bool WorldSizeDialog::Validate()
{
    return wxDialog::Validate() && typedExtent().has_value();
}

std::optional<WorldSizeRequest> WorldSizeDialog::request() const
{
    const std::expected<core::Extent, std::string> extent = typedExtent();
    if (!extent)
    {
        return std::nullopt;
    }
    return WorldSizeRequest{.extent = *extent, .keepPattern = keepPattern_->GetValue()};
}

std::expected<core::Extent, std::string> WorldSizeDialog::typedExtent() const
{
    const std::optional<core::Coord> width  = typedSide(*width_);
    const std::optional<core::Coord> height = typedSide(*height_);
    if (!width || !height)
    {
        return std::unexpected(std::string(kNotWholeNumbers));
    }
    const core::Extent extent{.width = *width, .height = *height};
    return core::validateExtent(extent, budget_).transform_error([&](core::ExtentError error) {
        return core::describe(error, extent, budget_);
    });
}

void WorldSizeDialog::applyPreset(int index)
{
    if (index < kFirstSquareIndex || index > kFitWindowIndex)
    {
        return;  // "Choose…" is only a caption
    }
    core::Extent extent = fitsCanvas_;
    if (index < kFitWindowIndex)
    {
        const core::Coord side =
            kSquareSides.at(static_cast<std::size_t>(index - kFirstSquareIndex));

        extent = {.width = side, .height = side};
    }
    // Programmatic SetValue sends no events, so revalidate() is called directly.
    width_->SetValue(extent.width);
    height_->SetValue(extent.height);
    presets_->SetSelection(kChooseIndex);
    revalidate();
}

void WorldSizeDialog::revalidate()
{
    const std::expected<core::Extent, std::string> extent = typedExtent();
    // Only a valid size has a memory figure; the error line explains the others.
    const std::string bytes = extent ? core::formatBytes(core::worldBytes(*extent)) : "–";
    memory_->SetLabelText(toWx(memoryText(bytes, budget_)));
    error_->SetLabelText(extent ? wxString() : toWx(extent.error()));
    ok_->Enable(extent.has_value());
}

}  // namespace wxLife::ui

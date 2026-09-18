#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

#include "wxLife/core/Types.h"

namespace wxLife::ui
{

/// What the user chose in WorldSizeDialog.
struct WorldSizeRequest
{
    core::Extent extent;
    bool         keepPattern = true;
};

/// Modal dialog for any width × height, checked live against the side limits and the memory budget.
class WorldSizeDialog final : public wxDialog
{
public:
    /// Shows the dialog. `fitsCanvas` is offered as the "Fit window" preset.
    /// @return nullopt when the user cancels.
    [[nodiscard]] static std::optional<WorldSizeRequest> ask(wxWindow* parent, core::Extent current,
                                                             core::Extent  fitsCanvas,
                                                             std::uint64_t memoryBudgetBytes);

    /// wx asks this before it accepts the dialog. It matters for Enter in a size box: wxGTK then
    /// presses OK even while it is disabled.
    bool Validate() override;

private:
    WorldSizeDialog(wxWindow* parent, core::Extent current, core::Extent fitsCanvas,
                    std::uint64_t memoryBudgetBytes);
    /// nullopt for an invalid size.
    [[nodiscard]] std::optional<WorldSizeRequest> request() const;
    /// The size as typed, or the message that says why it cannot be used.
    [[nodiscard]] std::expected<core::Extent, std::string> typedExtent() const;
    void                                                   applyPreset(int index);
    /// Updates the memory and error lines; enables OK only for valid sizes.
    void revalidate();

    core::Extent  fitsCanvas_;
    std::uint64_t budget_;
    // Child controls, owned by wx.
    wxSpinCtrl*   width_{};
    wxSpinCtrl*   height_{};
    wxChoice*     presets_{};
    wxCheckBox*   keepPattern_{};
    wxStaticText* memory_{};
    wxStaticText* error_{};
    wxButton*     ok_{};  ///< From CreateStdDialogButtonSizer(...)->GetAffirmativeButton()
};

}  // namespace wxLife::ui

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/radiobut.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

#include "wxLife/core/Types.h"

namespace wxLife::ui
{

/// What the user chose in WorldSizeDialog.
struct WorldSizeRequest
{
    core::WorldKind kind = core::WorldKind::FIXED_SIZE;
    core::Extent    extent;  ///< For a fixed-size world
    bool            keepPattern = true;
};

/// Modal dialog for the kind of world and, for a fixed-size one, any width × height, checked live
/// against the side limits and the memory budget.
class WorldSizeDialog final : public wxDialog
{
public:
    /// Shows the dialog. `fitsCanvas` is offered as the "Fit window" preset. A non-empty
    /// `unboundedRefusal` says why an unbounded world cannot be chosen now.
    /// @return nullopt when the user cancels.
    [[nodiscard]] static std::optional<WorldSizeRequest> ask(wxWindow* parent, core::WorldKind kind,
                                                             core::Extent     current,
                                                             core::Extent     fitsCanvas,
                                                             std::uint64_t    memoryBudgetBytes,
                                                             std::string_view unboundedRefusal);

    /// wx asks this before it accepts the dialog. It matters for Enter in a size box: wxGTK then
    /// presses OK even while it is disabled.
    bool Validate() override;

private:
    WorldSizeDialog(wxWindow* parent, core::WorldKind kind, core::Extent current,
                    core::Extent fitsCanvas, std::uint64_t memoryBudgetBytes,
                    std::string_view unboundedRefusal);
    [[nodiscard]] bool unboundedChosen() const;
    /// nullopt for an invalid size.
    [[nodiscard]] std::optional<WorldSizeRequest> request() const;
    /// The size as typed, or the message that says why it cannot be used.
    [[nodiscard]] std::expected<core::Extent, std::string> typedExtent() const;
    void                                                   applyPreset(int index);
    /// Updates the memory and error lines; enables OK only for valid sizes.
    void revalidate();

    core::Extent  m_fitsCanvas;
    std::uint64_t m_budget;
    std::string   m_unboundedRefusal;
    // Child controls, owned by wx.
    wxRadioButton* m_fixed{};
    wxRadioButton* m_unbounded{};
    wxSpinCtrl*    m_width{};
    wxSpinCtrl*    m_height{};
    wxChoice*      m_presets{};
    wxCheckBox*    m_keepPattern{};
    wxStaticText*  m_memory{};
    wxStaticText*  m_error{};
    wxButton*      m_ok{};  ///< From CreateStdDialogButtonSizer(...)->GetAffirmativeButton()
};

}  // namespace wxLife::ui

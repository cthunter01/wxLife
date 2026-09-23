#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/treectrl.h>

#include "wxLife/core/Pattern.h"

namespace wxLife::ui
{

/// Modal dialog that lists the demo patterns (core::demos()) by category and describes the one
/// selected: who made it, a preview, its size, the world it runs in and what to watch for.
class DemoDialog final : public wxDialog
{
public:
    /// Shows the dialog with the demo `selected` (an index into core::demos()) chosen, or the
    /// first demo. Demos that need more than `memoryBudgetBytes` are listed but cannot be loaded.
    /// @return the index of the demo to load; nullopt when the user cancels.
    [[nodiscard]] static std::optional<std::size_t> ask(wxWindow*                  parent,
                                                        std::optional<std::size_t> selected,
                                                        std::uint64_t memoryBudgetBytes);

    /// wx asks this before it accepts the dialog: a demo must be selected, and it must fit the
    /// budget. It matters for Enter too, which wxGTK turns into a press of Load even while Load is
    /// disabled.
    bool Validate() override;

private:
    DemoDialog(wxWindow* parent, std::optional<std::size_t> selected,
               std::uint64_t memoryBudgetBytes);
    /// The demo selected in the tree; nullopt for a category or nothing.
    [[nodiscard]] std::optional<std::size_t> selectedDemo() const;
    /// Whether the demo's world fits the memory budget.
    [[nodiscard]] bool fits(std::size_t demo) const;
    /// The demo's pattern, read the first time it is asked for.
    [[nodiscard]] const core::Pattern& pattern(std::size_t demo);
    /// Describes the selected demo and enables Load when it can be loaded.
    void showSelection();
    void showPreview(std::size_t demo);

    std::uint64_t                             m_budget;
    std::vector<std::optional<core::Pattern>> m_patterns;  ///< One per demo, read on first use.
    std::vector<wxTreeItemId>                 m_items;     ///< The tree item of each demo.
    // Child controls, owned by wx.
    wxTreeCtrl*     m_tree{};
    wxStaticText*   m_name{};
    wxStaticText*   m_credit{};
    wxStaticBitmap* m_preview{};
    wxStaticText*   m_facts{};
    wxStaticText*   m_about{};
    wxStaticText*   m_error{};
    wxButton*       m_load{};  ///< From CreateStdDialogButtonSizer(...)->GetAffirmativeButton()
};

}  // namespace wxLife::ui

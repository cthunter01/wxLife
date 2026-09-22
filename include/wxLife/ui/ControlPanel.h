#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include "wxLife/core/Ant.h"
#include "wxLife/core/Rule.h"
#include "wxLife/core/Speed.h"
#include "wxLife/core/Types.h"

namespace wxLife::ui
{

/// Side panel with the controls. Each user change is re-sent as a CommandId (see emitCommand).
/// Setters never emit, so MainFrame can push model state into the panel without feedback loops.
class ControlPanel final : public wxScrolledWindow
{
public:
    explicit ControlPanel(wxWindow* parent);

    /// Button label "Run" / "Pause".
    void setRunning(bool running);
    /// Selects the automaton and greys out whatever only the other one uses.
    void                          setAutomaton(core::Automaton automaton);
    [[nodiscard]] core::Automaton selectedAutomaton() const;
    /// Clamped to the control's range, so 0 ants still shows 1.
    void              setAntCount(int count);
    [[nodiscard]] int antCount() const;
    void              setSpeed(core::Speed speed);
    /// Spin value plus the Max check box.
    [[nodiscard]] core::Speed speed() const;
    void                      setCellSize(int px);
    /// Device pixels, 1..100.
    [[nodiscard]] int cellSize() const;
    void              setShowGrid(bool show);
    void              setWrap(bool wrap);
    /// Two lines: "512 × 512 cells" and "516 KiB".
    void setWorldInfo(core::Extent extent, std::uint64_t bytes);
    /// Canonical text, matching preset, error cleared.
    void setRule(const core::Rule& rule);
    /// UTF-8.
    [[nodiscard]] std::string ruleText() const;
    /// nullopt for "Custom".
    [[nodiscard]] std::optional<std::size_t> selectedPreset() const;
    /// An empty message hides the error line.
    void setRuleError(std::string_view message);
    void focusRuleText();
    /// 0.01 .. 1.0
    [[nodiscard]] double randomDensity() const;

private:
    void addSimulationGroup(wxSizer& column);
    void addSpeedGroup(wxSizer& column);
    void addViewGroup(wxSizer& column);
    void addWorldGroup(wxSizer& column);
    void addRuleGroup(wxSizer& column);

    // Child controls, owned by wx.
    wxButton*     m_runPause{};
    wxChoice*     m_automaton{};  ///< core::kAutomata names, in that order
    wxSpinCtrl*   m_density{};    ///< 1..100 %
    wxSpinCtrl*   m_antCount{};   ///< 1..core::kMaxAnts
    wxButton*     m_resetAnts{};
    wxSlider*     m_speedSlider{};  ///< 0..Speed::kSliderMax (log scale)
    wxSpinCtrl*   m_speedSpin{};    ///< Speed::kMin..kMax
    wxCheckBox*   m_maxSpeed{};
    wxSlider*     m_cellSizeSlider{};  ///< Index into render::kZoomSteps
    wxSpinCtrl*   m_cellSizeSpin{};    ///< 1..100 px
    wxCheckBox*   m_showGrid{};
    wxStaticText* m_worldInfo{};
    wxCheckBox*   m_wrap{};
    wxStaticBox*  m_ruleBox{};     ///< Disabling it greys out the whole Rule group at once
    wxChoice*     m_rulePreset{};  ///< kRulePresets names, then "Custom"
    wxTextCtrl*   m_ruleText{};    ///< wxTE_PROCESS_ENTER
    wxStaticText* m_ruleError{};
};

}  // namespace wxLife::ui

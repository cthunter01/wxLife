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
#include "wxLife/render/Types.h"

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
    void setAutomaton(core::Automaton automaton);
    /// Greys out what the kind of world does not use: an unbounded world runs only Life, has no
    /// edges to wrap, and is the only kind with a step size.
    void                          setWorldKind(core::WorldKind kind);
    [[nodiscard]] core::Automaton selectedAutomaton() const;
    /// Clamped to the control's range, so 0 ants still shows 1.
    void              setAntCount(int count);
    [[nodiscard]] int antCount() const;
    void              setSpeed(core::Speed speed);
    /// Spin value plus the Max check box.
    [[nodiscard]] core::Speed speed() const;
    /// Generations per step, 2^exponent; shows the count beside it.
    void                   setStepExponent(unsigned exponent);
    [[nodiscard]] unsigned stepExponent() const;
    /// The slider spans the zoom ladder from `maxShrink` levels below 1 px up to the largest
    /// render::kZoomSteps entry. Below 1 px the size box gives way to a text such as "1/16 px".
    void setScale(render::Scale scale, unsigned maxShrink);
    /// As last set, or as the user has since chosen with the slider or the size box.
    [[nodiscard]] render::Scale scale() const;
    void                        setShowGrid(bool show);
    void                        setWrap(bool wrap);
    /// Two lines: "512 × 512 cells" and "516 KiB".
    void setWorldInfo(core::Extent extent, std::uint64_t bytes);
    /// Two lines: "Unbounded plane" and "12.3 MiB in use".
    void setUnboundedInfo(std::uint64_t bytes);
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
    void setWorldInfoText(const wxString& text);
    /// Shows m_scale in the size box, or below 1 px in m_scaleText.
    void showScale();
    /// Enables the controls that the automaton and the kind of world use.
    void updateEnabled();

    // Child controls, owned by wx.
    wxButton*     m_runPause{};
    wxChoice*     m_automaton{};  ///< core::kAutomata names, in that order
    wxSpinCtrl*   m_density{};    ///< 1..100 %
    wxSpinCtrl*   m_antCount{};   ///< 1..core::kMaxAnts
    wxButton*     m_resetAnts{};
    wxSlider*     m_speedSlider{};  ///< 0..Speed::kSliderMax (log scale)
    wxSpinCtrl*   m_speedSpin{};    ///< Speed::kMin..kMax
    wxCheckBox*   m_maxSpeed{};
    wxStaticText* m_stepLabel{};
    wxSpinCtrl*   m_stepExponent{};  ///< 0..SimulationRunner::kMaxStepExponent
    wxStaticText* m_stepSize{};      ///< "4,096 generations per step"
    /// Places on the zoom ladder: kZoomSteps indices from 0 up, shrink levels from -1 down.
    wxSlider*     m_cellSizeSlider{};
    wxSpinCtrl*   m_cellSizeSpin{};  ///< 1..100 px; hidden below 1 px
    wxStaticText* m_cellSizeUnit{};  ///< "px", hidden with the size box
    wxStaticText* m_scaleText{};     ///< "1/16 px", shown only below 1 px
    wxCheckBox*   m_showGrid{};
    wxStaticText* m_worldInfo{};
    wxCheckBox*   m_wrap{};
    wxStaticBox*  m_ruleBox{};     ///< Disabling it greys out the whole Rule group at once
    wxChoice*     m_rulePreset{};  ///< kRulePresets names, then "Custom"
    wxTextCtrl*   m_ruleText{};    ///< wxTE_PROCESS_ENTER
    wxStaticText* m_ruleError{};

    render::Scale   m_scale;
    core::Automaton m_shownAutomaton = core::Automaton::LIFE;
    core::WorldKind m_shownKind      = core::WorldKind::FIXED_SIZE;
};

}  // namespace wxLife::ui

#include "wxLife/ui/ControlPanel.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string>

#include <wx/arrstr.h>
#include <wx/statbox.h>

#include "wxLife/core/Ant.h"
#include "wxLife/core/Format.h"
#include "wxLife/core/Rule.h"
#include "wxLife/core/Speed.h"
#include "wxLife/core/Types.h"
#include "wxLife/render/Viewport.h"
#include "wxLife/ui/CommandIds.h"
#include "wxLife/ui/Defaults.h"
#include "wxLife/ui/Theme.h"
#include "wxLife/ui/WxConvert.h"

namespace wxLife::ui
{

namespace
{

constexpr int kScrollStepDip     = 10;
constexpr int kMinDensityPercent = 1;
constexpr int kMaxDensityPercent = 100;

// Re-sends a control's own event as `id`; the command then propagates up to MainFrame.
template <typename Event>
void sendOn(wxWindow& control, const wxEventTypeTag<Event>& type, CommandId id)
{
    control.Bind(type, [&control, id](Event&) { emitCommand(control, id); });
}

// Margins shared by every row inside a group.
[[nodiscard]] wxSizerFlags rowFlags()
{
    return wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM);
}

// A row whose first item takes the spare width.
[[nodiscard]] wxBoxSizer* stretchRow(wxWindow* stretched, std::initializer_list<wxWindow*> rest)
{
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(stretched, wxSizerFlags(1).CentreVertical());
    for (wxWindow* item : rest)
    {
        row->Add(item, wxSizerFlags().CentreVertical().Border(wxLEFT));
    }
    return row;
}

// Buttons of equal width, two per row.
[[nodiscard]] wxGridSizer* buttonGrid(std::initializer_list<wxButton*> buttons)
{
    const int gap  = wxSizerFlags::GetDefaultBorder();
    auto*     grid = new wxGridSizer(2, gap, gap);
    for (wxButton* button : buttons)
    {
        grid->Add(button, wxSizerFlags().Expand());
    }
    return grid;
}

[[nodiscard]] wxSpinCtrl* makeSpin(wxWindow* parent, int min, int max, int initial)
{
    return new wxSpinCtrl(parent, wxID_ANY, wxString(), wxDefaultPosition, wxDefaultSize,
                          wxSP_ARROW_KEYS, min, max, initial);
}

}  // namespace

ControlPanel::ControlPanel(wxWindow* parent)
  : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                     wxVSCROLL | wxTAB_TRAVERSAL)
{
    auto* column = new wxBoxSizer(wxVERTICAL);
    addSimulationGroup(*column);
    addSpeedGroup(*column);
    addViewGroup(*column);
    addWorldGroup(*column);
    addRuleGroup(*column);
    SetSizer(column);
    // Vertical only, so short screens can reach every group.
    SetScrollRate(0, FromDIP(kScrollStepDip));

    // GTK changes these controls under the wheel even without the focus, so scrolling the panel
    // would silently change values. A consumed wheel event never reaches GTK.
    for (wxWindow* control : std::initializer_list<wxWindow*>{
             m_automaton, m_density, m_antCount, m_speedSlider, m_speedSpin, m_cellSizeSlider,
             m_cellSizeSpin, m_rulePreset})
    {
        control->Bind(wxEVT_MOUSEWHEEL, [control](wxMouseEvent& event) {
            if (control->HasFocus())
            {
                event.Skip();
            }
        });
    }
}

void ControlPanel::setRunning(bool running)
{
    m_runPause->SetLabel(running ? "Pause" : "Run");
}

// Setters use SetValue/SetSelection/ChangeValue, which never emit events, so there are no
// feedback loops.
void ControlPanel::setAutomaton(core::Automaton automaton)
{
    m_automaton->SetSelection(static_cast<int>(std::ranges::distance(
        core::kAutomata.begin(), std::ranges::find(core::kAutomata, automaton))));

    // Each automaton greys out what only the other one uses, so a dead control is visible as such.
    const bool life = automaton == core::Automaton::Life;
    m_antCount->Enable(!life);
    m_resetAnts->Enable(!life);
    m_wrap->Enable(life);  // the ant always wraps, whatever the topology says
    m_ruleBox->Enable(life);
}

core::Automaton ControlPanel::selectedAutomaton() const
{
    const int selection = m_automaton->GetSelection();
    if (selection < 0 || static_cast<std::size_t>(selection) >= core::kAutomata.size())
    {
        return core::Automaton::Life;
    }
    return core::kAutomata.at(static_cast<std::size_t>(selection));
}

void ControlPanel::setAntCount(int count)
{
    m_antCount->SetValue(count);
}

int ControlPanel::antCount() const
{
    return m_antCount->GetValue();
}

void ControlPanel::setSpeed(core::Speed speed)
{
    m_speedSpin->SetValue(speed.gensPerSecond);
    // Several slider positions share one rate; leave the slider where the user put it if it
    // already fits.
    if (core::Speed::fromSliderPosition(m_speedSlider->GetValue()) != speed.gensPerSecond)
    {
        m_speedSlider->SetValue(core::Speed::toSliderPosition(speed.gensPerSecond));
    }
    m_maxSpeed->SetValue(speed.unlimited);
    // At Max the rate is ignored; greying it out says so.
    m_speedSlider->Enable(!speed.unlimited);
    m_speedSpin->Enable(!speed.unlimited);
}

core::Speed ControlPanel::speed() const
{
    return {.gensPerSecond = m_speedSpin->GetValue(), .unlimited = m_maxSpeed->GetValue()};
}

void ControlPanel::setCellSize(int px)
{
    m_cellSizeSpin->SetValue(px);
    m_cellSizeSlider->SetValue(static_cast<int>(render::nearestZoomStep(px)));
}

int ControlPanel::cellSize() const
{
    return m_cellSizeSpin->GetValue();
}

void ControlPanel::setShowGrid(bool show)
{
    m_showGrid->SetValue(show);
}

void ControlPanel::setWrap(bool wrap)
{
    m_wrap->SetValue(wrap);
}

void ControlPanel::setWorldInfo(core::Extent extent, std::uint64_t bytes)
{
    const wxString text = toWx(std::format(
        "{} × {} cells\n{}", core::formatCount(static_cast<std::uint64_t>(extent.width)),
        core::formatCount(static_cast<std::uint64_t>(extent.height)), core::formatBytes(bytes)));
    if (text == m_worldInfo->GetLabelText())
    {
        return;
    }
    m_worldInfo->SetLabelText(text);
    Layout();  // the text may need a different width
}

void ControlPanel::setRule(const core::Rule& rule)
{
    m_ruleText->ChangeValue(toWx(rule.toString()));  // unlike SetValue, sends no wxEVT_TEXT
    // A rule without a preset selects "Custom", the entry after the presets.
    const std::optional<std::size_t> preset = core::findPreset(rule);
    m_rulePreset->SetSelection(static_cast<int>(preset.value_or(core::kRulePresets.size())));
    setRuleError({});
}

std::string ControlPanel::ruleText() const
{
    return toUtf8(m_ruleText->GetValue());
}

std::optional<std::size_t> ControlPanel::selectedPreset() const
{
    const int selection = m_rulePreset->GetSelection();
    if (selection < 0 || static_cast<std::size_t>(selection) >= core::kRulePresets.size())
    {
        return std::nullopt;
    }
    return static_cast<std::size_t>(selection);
}

void ControlPanel::setRuleError(std::string_view message)
{
    if (message.empty() && !m_ruleError->IsShown())
    {
        return;
    }
    // SetLabelText: an '&' is shown, not taken as a mnemonic.
    m_ruleError->SetLabelText(toWx(message));
    m_ruleError->Wrap(m_rulePreset->GetSize().GetWidth());  // the choice spans the whole group
    m_ruleError->Show(!message.empty());
    FitInside();  // the scrollable height changed
    Layout();
}

void ControlPanel::focusRuleText()
{
    m_ruleText->SetFocus();
    m_ruleText->SelectAll();
}

double ControlPanel::randomDensity() const
{
    return m_density->GetValue() / 100.0;
}

void ControlPanel::addSimulationGroup(wxSizer& column)
{
    auto* group = new wxStaticBoxSizer(wxVERTICAL, this, "Simulation");
    // wx 3 recommends the static box as the parent of the group's controls.
    wxStaticBox* box = group->GetStaticBox();

    wxArrayString automatonNames;
    for (const core::Automaton automaton : core::kAutomata)
    {
        automatonNames.Add(toWx(core::toString(automaton)));
    }
    m_automaton = new wxChoice(box, wxID_ANY, wxDefaultPosition, wxDefaultSize, automatonNames);
    m_automaton->SetSelection(0);
    m_automaton->SetToolTip("Which automaton the world runs");

    m_runPause      = new wxButton(box, wxID_ANY, "Run");
    auto* step      = new wxButton(box, wxID_ANY, "Step");
    auto* clear     = new wxButton(box, wxID_ANY, "Clear");
    auto* randomize = new wxButton(box, wxID_ANY, "Randomize");
    // Created before m_antCount, so the density stays the group's first wxSpinCtrl.
    m_density =
        makeSpin(box, kMinDensityPercent, kMaxDensityPercent, defaults::kRandomDensityPercent);
    m_density->SetToolTip("Share of live cells after Randomize");
    m_antCount = makeSpin(box, 1, core::kMaxAnts, defaults::kAntCount);
    m_antCount->SetToolTip("Ants on the world; each one moves once per generation");
    m_resetAnts = new wxButton(box, wxID_ANY, "Reset");

    sendOn(*m_automaton, wxEVT_CHOICE, AutomatonChangedID);
    sendOn(*m_runPause, wxEVT_BUTTON, RunPauseID);
    sendOn(*step, wxEVT_BUTTON, StepID);
    sendOn(*clear, wxEVT_BUTTON, ClearID);
    sendOn(*randomize, wxEVT_BUTTON, RandomizeID);
    // The density sends nothing: the Randomize handler reads it. Changing the count is a reset.
    sendOn(*m_antCount, wxEVT_SPINCTRL, ResetAntsID);
    sendOn(*m_resetAnts, wxEVT_BUTTON, ResetAntsID);

    group->Add(m_automaton, rowFlags());
    group->Add(buttonGrid({m_runPause, step, clear, randomize}), rowFlags());
    group->Add(stretchRow(new wxStaticText(box, wxID_ANY, "Density"),
                          {m_density, new wxStaticText(box, wxID_ANY, "%")}),
               rowFlags());
    group->Add(stretchRow(new wxStaticText(box, wxID_ANY, "Ants"), {m_antCount, m_resetAnts}),
               rowFlags());
    column.Add(group, wxSizerFlags().Expand().Border());
}

void ControlPanel::addSpeedGroup(wxSizer& column)
{
    auto*        group = new wxStaticBoxSizer(wxVERTICAL, this, "Speed");
    wxStaticBox* box   = group->GetStaticBox();

    m_speedSlider = new wxSlider(box, wxID_ANY, 0, 0, core::Speed::kSliderMax);
    m_speedSpin   = makeSpin(box, core::Speed::kMin, core::Speed::kMax, core::Speed::kMin);
    m_maxSpeed    = new wxCheckBox(box, wxID_ANY, "Max speed");
    m_speedSlider->SetToolTip("Generations per second (logarithmic)");

    // The slider and the spin control show the same value; each updates the other before sending.
    m_speedSlider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
        m_speedSpin->SetValue(core::Speed::fromSliderPosition(m_speedSlider->GetValue()));
        emitCommand(*m_speedSlider, SpeedChangedID);
    });
    m_speedSpin->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) {
        m_speedSlider->SetValue(core::Speed::toSliderPosition(m_speedSpin->GetValue()));
        emitCommand(*m_speedSpin, SpeedChangedID);
    });
    sendOn(*m_maxSpeed, wxEVT_CHECKBOX, ToggleMaxSpeedID);

    group->Add(stretchRow(m_speedSlider, {m_speedSpin}), rowFlags());
    group->Add(m_maxSpeed, rowFlags());
    column.Add(group, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM));
}

void ControlPanel::addViewGroup(wxSizer& column)
{
    auto*        group = new wxStaticBoxSizer(wxVERTICAL, this, "View");
    wxStaticBox* box   = group->GetStaticBox();

    const auto lastZoomStep = static_cast<int>(render::kZoomSteps.size() - 1);

    m_cellSizeSlider = new wxSlider(box, wxID_ANY, 0, 0, lastZoomStep);
    m_cellSizeSpin = makeSpin(box, render::kMinCellSize, render::kMaxCellSize, defaults::kCellSize);
    auto* fit      = new wxButton(box, wxID_ANY, "Fit");
    auto* center   = new wxButton(box, wxID_ANY, "Center");
    m_showGrid     = new wxCheckBox(box, wxID_ANY, "Grid lines");
    m_cellSizeSlider->SetToolTip("Cell size in screen pixels");

    // The slider moves along render::kZoomSteps; the spin control takes any size in between.
    m_cellSizeSlider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
        m_cellSizeSpin->SetValue(
            render::kZoomSteps.at(static_cast<std::size_t>(m_cellSizeSlider->GetValue())));
        emitCommand(*m_cellSizeSlider, CellSizeChangedID);
    });
    m_cellSizeSpin->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) {
        m_cellSizeSlider->SetValue(
            static_cast<int>(render::nearestZoomStep(m_cellSizeSpin->GetValue())));
        emitCommand(*m_cellSizeSpin, CellSizeChangedID);
    });
    sendOn(*fit, wxEVT_BUTTON, ZoomFitID);
    sendOn(*center, wxEVT_BUTTON, CenterViewID);
    sendOn(*m_showGrid, wxEVT_CHECKBOX, ToggleGridID);

    group->Add(
        stretchRow(m_cellSizeSlider, {m_cellSizeSpin, new wxStaticText(box, wxID_ANY, "px")}),
        rowFlags());
    group->Add(buttonGrid({fit, center}), rowFlags());
    group->Add(m_showGrid, rowFlags());
    column.Add(group, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM));
}

void ControlPanel::addWorldGroup(wxSizer& column)
{
    auto*        group = new wxStaticBoxSizer(wxVERTICAL, this, "World");
    wxStaticBox* box   = group->GetStaticBox();

    m_worldInfo  = new wxStaticText(box, wxID_ANY, wxString());
    auto* resize = new wxButton(box, wxID_ANY, toWx("Resize…"));
    m_wrap       = new wxCheckBox(box, wxID_ANY, "Wrap edges");
    m_wrap->SetToolTip("Opposite edges are neighbours (a torus)");

    sendOn(*resize, wxEVT_BUTTON, WorldSizeID);
    sendOn(*m_wrap, wxEVT_CHECKBOX, ToggleWrapID);

    group->Add(m_worldInfo, rowFlags());
    group->Add(buttonGrid({resize}), rowFlags());
    group->Add(m_wrap, rowFlags());
    column.Add(group, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM));
}

void ControlPanel::addRuleGroup(wxSizer& column)
{
    auto*        group = new wxStaticBoxSizer(wxVERTICAL, this, "Rule");
    wxStaticBox* box   = group->GetStaticBox();

    m_ruleBox = box;  // Life-only: setAutomaton() greys out the whole group through it

    wxArrayString presetNames;
    for (const core::NamedRule& preset : core::kRulePresets)
    {
        presetNames.Add(toWx(preset.name));
    }
    presetNames.Add("Custom");  // index kRulePresets.size()
    m_rulePreset = new wxChoice(box, wxID_ANY, wxDefaultPosition, wxDefaultSize, presetNames);
    m_ruleText   = new wxTextCtrl(box, wxID_ANY, wxString(), wxDefaultPosition, wxDefaultSize,
                                  wxTE_PROCESS_ENTER);
    auto* apply  = new wxButton(box, wxID_ANY, "Apply");
    m_ruleError  = new wxStaticText(box, wxID_ANY, wxString());
    m_ruleText->SetToolTip(
        "B/S notation: B = neighbour counts that give birth, S = counts that survive");

    useErrorColour(*m_ruleError);
    m_ruleError->Hide();

    sendOn(*m_rulePreset, wxEVT_CHOICE, RulePresetID);
    sendOn(*m_ruleText, wxEVT_TEXT_ENTER, ApplyRuleID);
    sendOn(*apply, wxEVT_BUTTON, ApplyRuleID);

    group->Add(m_rulePreset, rowFlags());
    group->Add(stretchRow(m_ruleText, {apply}), rowFlags());
    group->Add(m_ruleError, rowFlags());
    column.Add(group, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM));
}

}  // namespace wxLife::ui

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
    for (wxWindow* control :
         std::initializer_list<wxWindow*>{automaton_, density_, antCount_, speedSlider_, speedSpin_,
                                          cellSizeSlider_, cellSizeSpin_, rulePreset_})
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
    runPause_->SetLabel(running ? "Pause" : "Run");
}

// Setters use SetValue/SetSelection/ChangeValue, which never emit events, so there are no
// feedback loops.
void ControlPanel::setAutomaton(core::Automaton automaton)
{
    const auto* const found = std::ranges::find(core::kAutomata, automaton);
    automaton_->SetSelection(
        static_cast<int>(std::ranges::distance(core::kAutomata.begin(), found)));

    // Each automaton greys out what only the other one uses, so a dead control is visible as such.
    const bool life = automaton == core::Automaton::Life;
    antCount_->Enable(!life);
    resetAnts_->Enable(!life);
    wrap_->Enable(life);  // the ant always wraps, whatever the topology says
    ruleBox_->Enable(life);
}

core::Automaton ControlPanel::selectedAutomaton() const
{
    const int selection = automaton_->GetSelection();
    if (selection < 0 || static_cast<std::size_t>(selection) >= core::kAutomata.size())
    {
        return core::Automaton::Life;
    }
    return core::kAutomata.at(static_cast<std::size_t>(selection));
}

void ControlPanel::setAntCount(int count)
{
    antCount_->SetValue(count);
}

int ControlPanel::antCount() const
{
    return antCount_->GetValue();
}

void ControlPanel::setSpeed(core::Speed speed)
{
    speedSpin_->SetValue(speed.gensPerSecond);
    // Several slider positions share one rate; leave the slider where the user put it if it
    // already fits.
    if (core::Speed::fromSliderPosition(speedSlider_->GetValue()) != speed.gensPerSecond)
    {
        speedSlider_->SetValue(core::Speed::toSliderPosition(speed.gensPerSecond));
    }
    maxSpeed_->SetValue(speed.unlimited);
    // At Max the rate is ignored; greying it out says so.
    speedSlider_->Enable(!speed.unlimited);
    speedSpin_->Enable(!speed.unlimited);
}

core::Speed ControlPanel::speed() const
{
    return {.gensPerSecond = speedSpin_->GetValue(), .unlimited = maxSpeed_->GetValue()};
}

void ControlPanel::setCellSize(int px)
{
    cellSizeSpin_->SetValue(px);
    cellSizeSlider_->SetValue(static_cast<int>(render::nearestZoomStep(px)));
}

int ControlPanel::cellSize() const
{
    return cellSizeSpin_->GetValue();
}

void ControlPanel::setShowGrid(bool show)
{
    showGrid_->SetValue(show);
}

void ControlPanel::setWrap(bool wrap)
{
    wrap_->SetValue(wrap);
}

void ControlPanel::setWorldInfo(core::Extent extent, std::uint64_t bytes)
{
    const wxString text = toWx(std::format(
        "{} × {} cells\n{}", core::formatCount(static_cast<std::uint64_t>(extent.width)),
        core::formatCount(static_cast<std::uint64_t>(extent.height)), core::formatBytes(bytes)));
    if (text == worldInfo_->GetLabelText())
    {
        return;
    }
    worldInfo_->SetLabelText(text);
    Layout();  // the text may need a different width
}

void ControlPanel::setRule(const core::Rule& rule)
{
    ruleText_->ChangeValue(toWx(rule.toString()));  // unlike SetValue, sends no wxEVT_TEXT
    // A rule without a preset selects "Custom", the entry after the presets.
    const std::optional<std::size_t> preset = core::findPreset(rule);
    rulePreset_->SetSelection(static_cast<int>(preset.value_or(core::kRulePresets.size())));
    setRuleError({});
}

std::string ControlPanel::ruleText() const
{
    return toUtf8(ruleText_->GetValue());
}

std::optional<std::size_t> ControlPanel::selectedPreset() const
{
    const int selection = rulePreset_->GetSelection();
    if (selection < 0 || static_cast<std::size_t>(selection) >= core::kRulePresets.size())
    {
        return std::nullopt;
    }
    return static_cast<std::size_t>(selection);
}

void ControlPanel::setRuleError(std::string_view message)
{
    if (message.empty() && !ruleError_->IsShown())
    {
        return;
    }
    // SetLabelText: an '&' is shown, not taken as a mnemonic.
    ruleError_->SetLabelText(toWx(message));
    ruleError_->Wrap(rulePreset_->GetSize().GetWidth());  // the choice spans the whole group
    ruleError_->Show(!message.empty());
    FitInside();  // the scrollable height changed
    Layout();
}

void ControlPanel::focusRuleText()
{
    ruleText_->SetFocus();
    ruleText_->SelectAll();
}

double ControlPanel::randomDensity() const
{
    return density_->GetValue() / 100.0;
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
    automaton_ = new wxChoice(box, wxID_ANY, wxDefaultPosition, wxDefaultSize, automatonNames);
    automaton_->SetSelection(0);
    automaton_->SetToolTip("Which automaton the world runs");

    runPause_       = new wxButton(box, wxID_ANY, "Run");
    auto* step      = new wxButton(box, wxID_ANY, "Step");
    auto* clear     = new wxButton(box, wxID_ANY, "Clear");
    auto* randomize = new wxButton(box, wxID_ANY, "Randomize");
    // Created before antCount_, so the density stays the group's first wxSpinCtrl.
    density_ =
        makeSpin(box, kMinDensityPercent, kMaxDensityPercent, defaults::kRandomDensityPercent);
    density_->SetToolTip("Share of live cells after Randomize");
    antCount_ = makeSpin(box, 1, core::kMaxAnts, defaults::kAntCount);
    antCount_->SetToolTip("Ants on the world; each one moves once per generation");
    resetAnts_ = new wxButton(box, wxID_ANY, "Reset");

    sendOn(*automaton_, wxEVT_CHOICE, ID_AUTOMATON_CHANGED);
    sendOn(*runPause_, wxEVT_BUTTON, ID_RUN_PAUSE);
    sendOn(*step, wxEVT_BUTTON, ID_STEP);
    sendOn(*clear, wxEVT_BUTTON, ID_CLEAR);
    sendOn(*randomize, wxEVT_BUTTON, ID_RANDOMIZE);
    // The density sends nothing: the Randomize handler reads it. Changing the count is a reset.
    sendOn(*antCount_, wxEVT_SPINCTRL, ID_RESET_ANTS);
    sendOn(*resetAnts_, wxEVT_BUTTON, ID_RESET_ANTS);

    group->Add(automaton_, rowFlags());
    group->Add(buttonGrid({runPause_, step, clear, randomize}), rowFlags());
    group->Add(stretchRow(new wxStaticText(box, wxID_ANY, "Density"),
                          {density_, new wxStaticText(box, wxID_ANY, "%")}),
               rowFlags());
    group->Add(stretchRow(new wxStaticText(box, wxID_ANY, "Ants"), {antCount_, resetAnts_}),
               rowFlags());
    column.Add(group, wxSizerFlags().Expand().Border());
}

void ControlPanel::addSpeedGroup(wxSizer& column)
{
    auto*        group = new wxStaticBoxSizer(wxVERTICAL, this, "Speed");
    wxStaticBox* box   = group->GetStaticBox();

    speedSlider_ = new wxSlider(box, wxID_ANY, 0, 0, core::Speed::kSliderMax);
    speedSpin_   = makeSpin(box, core::Speed::kMin, core::Speed::kMax, core::Speed::kMin);
    maxSpeed_    = new wxCheckBox(box, wxID_ANY, "Max speed");
    speedSlider_->SetToolTip("Generations per second (logarithmic)");

    // The slider and the spin control show the same value; each updates the other before sending.
    speedSlider_->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
        speedSpin_->SetValue(core::Speed::fromSliderPosition(speedSlider_->GetValue()));
        emitCommand(*speedSlider_, ID_SPEED_CHANGED);
    });
    speedSpin_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) {
        speedSlider_->SetValue(core::Speed::toSliderPosition(speedSpin_->GetValue()));
        emitCommand(*speedSpin_, ID_SPEED_CHANGED);
    });
    sendOn(*maxSpeed_, wxEVT_CHECKBOX, ID_TOGGLE_MAX_SPEED);

    group->Add(stretchRow(speedSlider_, {speedSpin_}), rowFlags());
    group->Add(maxSpeed_, rowFlags());
    column.Add(group, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM));
}

void ControlPanel::addViewGroup(wxSizer& column)
{
    auto*        group = new wxStaticBoxSizer(wxVERTICAL, this, "View");
    wxStaticBox* box   = group->GetStaticBox();

    const auto lastZoomStep = static_cast<int>(render::kZoomSteps.size() - 1);

    cellSizeSlider_ = new wxSlider(box, wxID_ANY, 0, 0, lastZoomStep);
    cellSizeSpin_ = makeSpin(box, render::kMinCellSize, render::kMaxCellSize, defaults::kCellSize);
    auto* fit     = new wxButton(box, wxID_ANY, "Fit");
    auto* center  = new wxButton(box, wxID_ANY, "Center");
    showGrid_     = new wxCheckBox(box, wxID_ANY, "Grid lines");
    cellSizeSlider_->SetToolTip("Cell size in screen pixels");

    // The slider moves along render::kZoomSteps; the spin control takes any size in between.
    cellSizeSlider_->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
        cellSizeSpin_->SetValue(
            render::kZoomSteps.at(static_cast<std::size_t>(cellSizeSlider_->GetValue())));
        emitCommand(*cellSizeSlider_, ID_CELL_SIZE_CHANGED);
    });
    cellSizeSpin_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) {
        cellSizeSlider_->SetValue(
            static_cast<int>(render::nearestZoomStep(cellSizeSpin_->GetValue())));
        emitCommand(*cellSizeSpin_, ID_CELL_SIZE_CHANGED);
    });
    sendOn(*fit, wxEVT_BUTTON, ID_ZOOM_FIT);
    sendOn(*center, wxEVT_BUTTON, ID_CENTER_VIEW);
    sendOn(*showGrid_, wxEVT_CHECKBOX, ID_TOGGLE_GRID);

    group->Add(stretchRow(cellSizeSlider_, {cellSizeSpin_, new wxStaticText(box, wxID_ANY, "px")}),
               rowFlags());
    group->Add(buttonGrid({fit, center}), rowFlags());
    group->Add(showGrid_, rowFlags());
    column.Add(group, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM));
}

void ControlPanel::addWorldGroup(wxSizer& column)
{
    auto*        group = new wxStaticBoxSizer(wxVERTICAL, this, "World");
    wxStaticBox* box   = group->GetStaticBox();

    worldInfo_   = new wxStaticText(box, wxID_ANY, wxString());
    auto* resize = new wxButton(box, wxID_ANY, toWx("Resize…"));
    wrap_        = new wxCheckBox(box, wxID_ANY, "Wrap edges");
    wrap_->SetToolTip("Opposite edges are neighbours (a torus)");

    sendOn(*resize, wxEVT_BUTTON, ID_WORLD_SIZE);
    sendOn(*wrap_, wxEVT_CHECKBOX, ID_TOGGLE_WRAP);

    group->Add(worldInfo_, rowFlags());
    group->Add(buttonGrid({resize}), rowFlags());
    group->Add(wrap_, rowFlags());
    column.Add(group, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM));
}

void ControlPanel::addRuleGroup(wxSizer& column)
{
    auto*        group = new wxStaticBoxSizer(wxVERTICAL, this, "Rule");
    wxStaticBox* box   = group->GetStaticBox();

    ruleBox_ = box;  // Life-only: setAutomaton() greys out the whole group through it

    wxArrayString presetNames;
    for (const core::NamedRule& preset : core::kRulePresets)
    {
        presetNames.Add(toWx(preset.name));
    }
    presetNames.Add("Custom");  // index kRulePresets.size()
    rulePreset_ = new wxChoice(box, wxID_ANY, wxDefaultPosition, wxDefaultSize, presetNames);
    ruleText_   = new wxTextCtrl(box, wxID_ANY, wxString(), wxDefaultPosition, wxDefaultSize,
                                 wxTE_PROCESS_ENTER);
    auto* apply = new wxButton(box, wxID_ANY, "Apply");
    ruleError_  = new wxStaticText(box, wxID_ANY, wxString());
    ruleText_->SetToolTip(
        "B/S notation: B = neighbour counts that give birth, S = counts that survive");

    useErrorColour(*ruleError_);
    ruleError_->Hide();

    sendOn(*rulePreset_, wxEVT_CHOICE, ID_RULE_PRESET);
    sendOn(*ruleText_, wxEVT_TEXT_ENTER, ID_APPLY_RULE);
    sendOn(*apply, wxEVT_BUTTON, ID_APPLY_RULE);

    group->Add(rulePreset_, rowFlags());
    group->Add(stretchRow(ruleText_, {apply}), rowFlags());
    group->Add(ruleError_, rowFlags());
    column.Add(group, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM));
}

}  // namespace wxLife::ui

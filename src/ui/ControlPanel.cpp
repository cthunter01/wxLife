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
#include "wxLife/render/Types.h"
#include "wxLife/render/Viewport.h"
#include "wxLife/ui/CommandIds.h"
#include "wxLife/ui/Defaults.h"
#include "wxLife/ui/SimulationRunner.h"
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
    setStepExponent(0);
    updateEnabled();

    // GTK changes these controls under the wheel even without the focus, so scrolling the panel
    // would silently change values. A consumed wheel event never reaches GTK.
    for (wxWindow* control : std::initializer_list<wxWindow*>{
             m_automaton, m_density, m_antCount, m_speedSlider, m_speedSpin, m_stepExponent,
             m_cellSizeSlider, m_cellSizeSpin, m_rulePreset})
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
    m_shownAutomaton = automaton;
    updateEnabled();
}

void ControlPanel::setWorldKind(core::WorldKind kind)
{
    m_shownKind = kind;
    updateEnabled();
}

void ControlPanel::updateEnabled()
{
    // Each automaton greys out what only the other one uses, and so does each kind of world, so a
    // dead control is visible as such.
    const bool life      = m_shownAutomaton == core::Automaton::LIFE;
    const bool unbounded = m_shownKind == core::WorldKind::UNBOUNDED;
    m_automaton->Enable(!unbounded);  // a plane runs only Life
    m_antCount->Enable(!life);
    m_resetAnts->Enable(!life);
    m_wrap->Enable(life && !unbounded);  // the ant always wraps; a plane has no edges
    m_ruleBox->Enable(life);
    m_stepLabel->Enable(unbounded);
    m_stepExponent->Enable(unbounded);
    m_stepSize->Enable(unbounded);
}

core::Automaton ControlPanel::selectedAutomaton() const
{
    const int selection = m_automaton->GetSelection();
    if (selection < 0 || static_cast<std::size_t>(selection) >= core::kAutomata.size())
    {
        return core::Automaton::LIFE;
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

void ControlPanel::setStepExponent(unsigned exponent)
{
    m_stepExponent->SetValue(static_cast<int>(exponent));
    // Exact counts while they stay readable; powers of two beyond.
    constexpr unsigned kLongestCount = 32;
    const std::string  count         = exponent <= kLongestCount
                                           ? core::formatCount(std::uint64_t{1} << exponent)
                                           : std::format("2^{}", exponent);
    const wxString     text =
        toWx(std::format("{} generation{} per step", count, exponent == 0 ? "" : "s"));
    if (text != m_stepSize->GetLabelText())
    {
        m_stepSize->SetLabelText(text);
        Layout();
    }
}

unsigned ControlPanel::stepExponent() const
{
    return static_cast<unsigned>(std::max(m_stepExponent->GetValue(), 0));
}

void ControlPanel::setScale(render::Scale scale, unsigned maxShrink)
{
    m_scale = scale;
    // The lower end follows the limit, which changes with the world and the canvas; a view zoomed
    // out further than that still finds its own place.
    const int lowest = -static_cast<int>(std::max(maxShrink, scale.shrink));
    if (m_cellSizeSlider->GetMin() != lowest)
    {
        m_cellSizeSlider->SetRange(lowest, m_cellSizeSlider->GetMax());
    }
    m_cellSizeSlider->SetValue(scale.zoomedOut()
                                   ? -static_cast<int>(scale.shrink)
                                   : static_cast<int>(render::nearestZoomStep(scale.cellSize)));
    showScale();
}

render::Scale ControlPanel::scale() const
{
    return m_scale;
}

void ControlPanel::showScale()
{
    const bool zoomedOut = m_scale.zoomedOut();
    bool       relayout  = m_cellSizeSpin->IsShown() == zoomedOut;
    if (zoomedOut)
    {
        const wxString text = toWx(render::toString(m_scale));
        relayout            = relayout || text != m_scaleText->GetLabelText();
        m_scaleText->SetLabelText(text);
    }
    else
    {
        m_cellSizeSpin->SetValue(m_scale.cellSize);
    }
    if (relayout)  // another control, or a text of another width
    {
        m_cellSizeSpin->Show(!zoomedOut);
        m_cellSizeUnit->Show(!zoomedOut);
        m_scaleText->Show(zoomedOut);
        Layout();
    }
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
    setWorldInfoText(toWx(std::format(
        "{} × {} cells\n{}", core::formatCount(static_cast<std::uint64_t>(extent.width)),
        core::formatCount(static_cast<std::uint64_t>(extent.height)), core::formatBytes(bytes))));
}

void ControlPanel::setUnboundedInfo(std::uint64_t bytes)
{
    setWorldInfoText(toWx(std::format("Unbounded plane\n{} in use", core::formatBytes(bytes))));
}

void ControlPanel::setWorldInfoText(const wxString& text)
{
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

    sendOn(*m_automaton, wxEVT_CHOICE, ID_AUTOMATON_CHANGED);
    sendOn(*m_runPause, wxEVT_BUTTON, ID_RUN_PAUSE);
    sendOn(*step, wxEVT_BUTTON, ID_STEP);
    sendOn(*clear, wxEVT_BUTTON, ID_CLEAR);
    sendOn(*randomize, wxEVT_BUTTON, ID_RANDOMIZE);
    // The density sends nothing: the Randomize handler reads it. Changing the count is a reset.
    sendOn(*m_antCount, wxEVT_SPINCTRL, ID_RESET_ANTS);
    sendOn(*m_resetAnts, wxEVT_BUTTON, ID_RESET_ANTS);

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

    m_speedSlider  = new wxSlider(box, wxID_ANY, 0, 0, core::Speed::kSliderMax);
    m_speedSpin    = makeSpin(box, core::Speed::kMin, core::Speed::kMax, core::Speed::kMin);
    m_maxSpeed     = new wxCheckBox(box, wxID_ANY, "Max speed");
    m_stepLabel    = new wxStaticText(box, wxID_ANY, "Step 2^");
    m_stepExponent = makeSpin(box, 0, static_cast<int>(SimulationRunner::kMaxStepExponent), 0);
    m_stepSize     = new wxStaticText(box, wxID_ANY, wxString());
    m_speedSlider->SetToolTip("Generations per second (logarithmic)");
    m_stepExponent->SetToolTip(
        "Unbounded worlds jump 2^n generations per step; HashLife makes large jumps cheap");

    // The slider and the spin control show the same value; each updates the other before sending.
    m_speedSlider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
        m_speedSpin->SetValue(core::Speed::fromSliderPosition(m_speedSlider->GetValue()));
        emitCommand(*m_speedSlider, ID_SPEED_CHANGED);
    });
    m_speedSpin->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) {
        m_speedSlider->SetValue(core::Speed::toSliderPosition(m_speedSpin->GetValue()));
        emitCommand(*m_speedSpin, ID_SPEED_CHANGED);
    });
    sendOn(*m_maxSpeed, wxEVT_CHECKBOX, ID_TOGGLE_MAX_SPEED);
    sendOn(*m_stepExponent, wxEVT_SPINCTRL, ID_STEP_SIZE_CHANGED);

    group->Add(stretchRow(m_speedSlider, {m_speedSpin}), rowFlags());
    group->Add(m_maxSpeed, rowFlags());
    group->Add(stretchRow(m_stepLabel, {m_stepExponent}), rowFlags());
    group->Add(m_stepSize, rowFlags());
    column.Add(group, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM));
}

void ControlPanel::addViewGroup(wxSizer& column)
{
    auto*        group = new wxStaticBoxSizer(wxVERTICAL, this, "View");
    wxStaticBox* box   = group->GetStaticBox();

    const auto lastZoomStep = static_cast<int>(render::kZoomSteps.size() - 1);

    m_cellSizeSlider = new wxSlider(box, wxID_ANY, 0, 0, lastZoomStep);
    m_cellSizeSpin = makeSpin(box, render::kMinCellSize, render::kMaxCellSize, defaults::kCellSize);
    m_cellSizeUnit = new wxStaticText(box, wxID_ANY, "px");
    m_scaleText    = new wxStaticText(box, wxID_ANY, wxString());
    m_scale        = {.cellSize = defaults::kCellSize};
    auto* fit      = new wxButton(box, wxID_ANY, "Fit");
    auto* center   = new wxButton(box, wxID_ANY, "Center");
    m_showGrid     = new wxCheckBox(box, wxID_ANY, "Grid lines");
    m_cellSizeSlider->SetToolTip("Cell size in screen pixels");

    // The slider moves along the zoom ladder; the spin control takes any size in between from
    // 1 px on.
    m_cellSizeSlider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
        const int place = m_cellSizeSlider->GetValue();
        m_scale =
            place < 0
                ? render::Scale{.shrink = static_cast<unsigned>(-place)}
                : render::Scale{.cellSize = render::kZoomSteps.at(static_cast<std::size_t>(place))};
        showScale();
        emitCommand(*m_cellSizeSlider, ID_CELL_SIZE_CHANGED);
    });
    m_cellSizeSpin->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) {
        m_scale = {.cellSize = m_cellSizeSpin->GetValue()};
        m_cellSizeSlider->SetValue(static_cast<int>(render::nearestZoomStep(m_scale.cellSize)));
        emitCommand(*m_cellSizeSpin, ID_CELL_SIZE_CHANGED);
    });
    sendOn(*fit, wxEVT_BUTTON, ID_ZOOM_FIT);
    sendOn(*center, wxEVT_BUTTON, ID_CENTER_VIEW);
    sendOn(*m_showGrid, wxEVT_CHECKBOX, ID_TOGGLE_GRID);

    group->Add(stretchRow(m_cellSizeSlider, {m_cellSizeSpin, m_cellSizeUnit, m_scaleText}),
               rowFlags());
    m_scaleText->Hide();
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
    auto* demos  = new wxButton(box, wxID_ANY, toWx("Demos…"));
    m_wrap       = new wxCheckBox(box, wxID_ANY, "Wrap edges");
    demos->SetToolTip("Famous patterns, each in a world set up for it");
    m_wrap->SetToolTip("Opposite edges are neighbours (a torus)");

    sendOn(*resize, wxEVT_BUTTON, ID_WORLD_SIZE);
    sendOn(*demos, wxEVT_BUTTON, ID_DEMO_PATTERNS);
    sendOn(*m_wrap, wxEVT_CHECKBOX, ID_TOGGLE_WRAP);

    group->Add(m_worldInfo, rowFlags());
    group->Add(buttonGrid({resize, demos}), rowFlags());
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

    sendOn(*m_rulePreset, wxEVT_CHOICE, ID_RULE_PRESET);
    sendOn(*m_ruleText, wxEVT_TEXT_ENTER, ID_APPLY_RULE);
    sendOn(*apply, wxEVT_BUTTON, ID_APPLY_RULE);

    group->Add(m_rulePreset, rowFlags());
    group->Add(stretchRow(m_ruleText, {apply}), rowFlags());
    group->Add(m_ruleError, rowFlags());
    column.Add(group, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM));
}

}  // namespace wxLife::ui

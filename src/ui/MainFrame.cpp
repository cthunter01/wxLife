#include "wxLife/ui/MainFrame.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <new>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <wx/aboutdlg.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statusbr.h>
#include <wx/utils.h>

#include "wxLife/core/Format.h"
#include "wxLife/core/Pacer.h"
#include "wxLife/core/ReferenceStepper.h"
#include "wxLife/core/Rule.h"
#include "wxLife/core/Speed.h"
#include "wxLife/core/Stepper.h"
#include "wxLife/core/Types.h"
#include "wxLife/core/World.h"
#include "wxLife/core/WorldLimits.h"
#include "wxLife/render/RenderStyle.h"
#include "wxLife/ui/CommandIds.h"
#include "wxLife/ui/ControlPanel.h"
#include "wxLife/ui/Defaults.h"
#include "wxLife/ui/MenuBar.h"
#include "wxLife/ui/SimulationRunner.h"
#include "wxLife/ui/WorldCanvas.h"
#include "wxLife/ui/WorldSizeDialog.h"
#include "wxLife/ui/WxConvert.h"

namespace wxLife::ui
{

namespace
{

// State, generation, population, speed, world, view. Negative widths are proportional.
constexpr std::array kStatusWidths{-1, -1, -1, -2, -3, -2};

constexpr std::string_view kControlsHelp = R"(Mouse on the world
    Left drag: draw (starting on a live cell erases)
    Right drag: erase
    Middle drag or Shift+left drag: pan
    Ctrl+left click: add or remove an ant (Langton's ant)
    Wheel: scroll (Shift: horizontally)
    Ctrl+wheel: zoom at the pointer

Keys while the world has focus (click it first)
    Space: run or pause
    N: step one generation
    ] and [: faster and slower
    + and -: zoom in and out
    F: fit the world    C or Home: center it
    G: grid lines    W: wrap edges
    Arrows: pan 10% (Shift: 90%)    Page Up/Down: pan 90%
    Esc: end a stroke

Everywhere (a focused text or number box keeps its own editing
keys, such as Ctrl+Home and Ctrl+Delete)
    F5: run or pause    F6: step
    Ctrl+] and Ctrl+[: faster and slower    Ctrl+M: max speed
    Ctrl+R: randomize    Ctrl+Delete: clear    Ctrl+L: edit the rule
    Ctrl+N: world size    Ctrl+T: wrap edges
    Ctrl+= and Ctrl+-: zoom    Ctrl+0: fit    Ctrl+Home: center    Ctrl+G: grid lines
    F1: this help    Ctrl+Q: quit)";

#ifdef __WXGTK__
// wx's generic status bar, which wxGTK uses, repaints the window synchronously (Update()) after
// every text change. Under X11 that paint can come before GTK has laid out a slider moved in the
// same handler, and the slider then keeps showing its old position. A plain refresh lets GTK lay
// everything out first. (wxMSW's and wxOSX's status bars are not wxStatusBarGeneric.)
class StatusBar final : public wxStatusBarGeneric
{
public:
    explicit StatusBar(wxWindow* parent) : wxStatusBarGeneric(parent) { }

protected:
    void DoUpdateStatusText(int field) override
    {
        wxRect rect;
        if (GetFieldRect(field, rect))
        {
            RefreshRect(rect);
        }
    }
};
#else
using StatusBar = wxStatusBar;
#endif

[[nodiscard]] std::uint64_t freshSeed()
{
    // random_device may be deterministic on some platforms, so the clock is mixed in.
    std::random_device device;
    const auto time = static_cast<std::uint64_t>(core::Clock::now().time_since_epoch().count());
    return ((std::uint64_t{device()} << 32) | device()) ^ time;
}

[[nodiscard]] std::string countText(std::int64_t n)
{
    return core::formatCount(static_cast<std::uint64_t>(n));
}

// The Engine menu item of each engine. There is no default, so -Wswitch points here when an
// engine is added.
[[nodiscard]] constexpr CommandId engineMenuItem(core::StepperKind kind) noexcept
{
    switch (kind)
    {
        case core::StepperKind::Banded:
            return EngineBandedID;
        case core::StepperKind::Reference:
            return EngineReferenceID;
    }
    std::unreachable();
}

// The same for the Automaton menu, and likewise without a default.
[[nodiscard]] constexpr CommandId automatonMenuItem(core::Automaton automaton) noexcept
{
    switch (automaton)
    {
        case core::Automaton::Life:
            return AutomatonLifeID;
        case core::Automaton::LangtonAnt:
            return AutomatonAntID;
    }
    std::unreachable();
}

// The world field of the status bar: what the running automaton actually uses. Life's text is
// also what the GUI smoke test pins, so it must stay exactly as it is.
[[nodiscard]] std::string worldText(const core::World& world)
{
    const core::Extent extent = world.extent();
    const std::string  size =
        std::format("{} × {}", countText(extent.width), countText(extent.height));
    switch (world.automaton())
    {
        case core::Automaton::Life:
            return std::format("{} · {} · {} · {}", size, core::toString(world.topology()),
                               world.rule().toString(), core::toString(world.stepper().kind()));
        case core::Automaton::LangtonAnt:
        {
            const std::size_t ants = world.ants().size();
            return std::format("{} · {} · {} ant{}", size, core::toString(world.automaton()), ants,
                               ants == 1 ? "" : "s");
        }
    }
    std::unreachable();
}

}  // namespace

MainFrame::MainFrame(core::World& world)
  : wxFrame(nullptr, wxID_ANY, "wxLife"),
    m_world(world),
    m_runner(world, [this](const TickReport& report) { onSimulationTick(report); })
{
    SetMenuBar(buildMenuBar());
    auto* statusBar = new StatusBar(this);
    statusBar->SetFieldsCount(static_cast<int>(kStatusWidths.size()), kStatusWidths.data());
    SetStatusBar(statusBar);
    // No menu help: the items have none, and wx would blank "Running" to show it.
    SetStatusBarPane(-1);
    buildLayout();
    bindCommands();

    m_runner.setSpeed(defaults::kSpeed);
    m_world.randomize(m_panel->randomDensity(), freshSeed());
    m_panel->setRule(m_world.rule());
    syncControls();
    updateStatusBar(true);

    SetMinSize(FromDIP(wxSize(defaults::kMinFrameWidthDip, defaults::kMinFrameHeightDip)));
    SetSize(FromDIP(wxSize(defaults::kFrameWidthDip, defaults::kFrameHeightDip)));
    Centre();
    // The world stays fitted until the user moves the camera, so it follows the frame's real size.
    m_canvas->fitWorld();
    m_canvas->SetFocus();
}

void MainFrame::buildLayout()
{
    m_panel = new ControlPanel(this);
    m_panel->SetMinSize(FromDIP(wxSize(defaults::kControlPanelWidthDip, -1)));
    m_canvas = new WorldCanvas(
        this, m_world,
        {
            .paintCells   = [this](std::span<const core::CellPos> cells,
                                   core::Cell value) { onPaintCells(cells, value); },
            .toggleAnt    = [this](core::CellPos cell) { onToggleAnt(cell); },
            .viewChanged  = [this] { onViewChanged(); },
            .hoverChanged = [this](std::optional<core::CellPos> cell) { onHoverChanged(cell); },
        });

    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(m_panel, wxSizerFlags(0).Expand());
    row->Add(m_canvas, wxSizerFlags(1).Expand());
    SetSizer(row);
}

void MainFrame::bindCommands()
{
    using Handler                = void (MainFrame::*)();
    static constexpr auto kTable = std::to_array<std::pair<CommandId, Handler>>({
        {RunPauseID, &MainFrame::onRunPause},
        {StepID, &MainFrame::onStep},
        {ClearID, &MainFrame::onClear},
        {RandomizeID, &MainFrame::onRandomize},
        {FasterID, &MainFrame::onFaster},
        {SlowerID, &MainFrame::onSlower},
        {ToggleMaxSpeedID, &MainFrame::onToggleMaxSpeed},
        {SpeedChangedID, &MainFrame::onSpeedChanged},
        {AutomatonLifeID, &MainFrame::onAutomatonLife},
        {AutomatonAntID, &MainFrame::onAutomatonAnt},
        {AutomatonChangedID, &MainFrame::onAutomatonChanged},
        {ResetAntsID, &MainFrame::onResetAnts},
        {EngineBandedID, &MainFrame::onEngineBanded},
        {EngineReferenceID, &MainFrame::onEngineReference},
        {ZoomInID, &MainFrame::onZoomIn},
        {ZoomOutID, &MainFrame::onZoomOut},
        {ZoomFitID, &MainFrame::onZoomFit},
        {CenterViewID, &MainFrame::onCenterView},
        {CellSizeChangedID, &MainFrame::onCellSizeChanged},
        {ToggleGridID, &MainFrame::onToggleGrid},
        {WorldSizeID, &MainFrame::onWorldSize},
        {ToggleWrapID, &MainFrame::onToggleWrap},
        {ApplyRuleID, &MainFrame::onApplyRule},
        {RulePresetID, &MainFrame::onRulePreset},
        {FocusRuleID, &MainFrame::onFocusRule},
        {ShowControlsHelpID, &MainFrame::onShowControlsHelp},
    });
    for (const auto& [id, handler] : kTable)
    {
        const auto run = [this, id, handler](wxCommandEvent& event) {
            (this->*handler)();
            // GTK leaves the focus on a clicked button or check box, and Space would press it
            // again. The canvas takes the focus back, except after Apply, so that a rejected rule
            // can be fixed.
            const wxObject* source = event.GetEventObject();
            if (id != ApplyRuleID &&
                (dynamic_cast<const wxButton*>(source) || dynamic_cast<const wxCheckBox*>(source)))
            {
                m_canvas->SetFocus();
            }
        };
        Bind(wxEVT_MENU, run, id);
    }
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { onAbout(); }, wxID_ABOUT);
    // wxGTK turns Ctrl+M into Enter inside a text box, where it would apply the rule. The char hook
    // sees the key before any control does.
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
        if (event.GetModifiers() == wxMOD_CONTROL && event.GetKeyCode() == 'M')
        {
            emitCommand(*this, ToggleMaxSpeedID);
        }
        else
        {
            event.Skip();
        }
    });
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::onClose, this);
}

void MainFrame::onRunPause()
{
    m_runner.toggle();
    syncControls();
    updateStatusBar(true);
}

void MainFrame::onStep()
{
    m_runner.stepOnce();  // reports through onSimulationTick()
    syncControls();
    updateStatusBar(true);  // the tick's own update may have been throttled
}

void MainFrame::onClear()
{
    m_world.clear();
    worldContentChanged();
}

void MainFrame::onRandomize()
{
    m_world.randomize(m_panel->randomDensity(), freshSeed());
    worldContentChanged();
}

void MainFrame::onFaster()
{
    m_runner.setSpeed(m_runner.speed().faster());
    syncControls();
    updateStatusBar(true);
}

void MainFrame::onSlower()
{
    m_runner.setSpeed(m_runner.speed().slower());
    syncControls();
    updateStatusBar(true);
}

void MainFrame::onToggleMaxSpeed()
{
    core::Speed speed = m_runner.speed();

    speed.unlimited = !speed.unlimited;
    m_runner.setSpeed(speed);
    syncControls();
    updateStatusBar(true);
}

void MainFrame::onSpeedChanged()
{
    m_runner.setSpeed(m_panel->speed());
    syncControls();
    updateStatusBar(true);
}

void MainFrame::onAutomatonLife()
{
    setAutomaton(core::Automaton::Life);
}

void MainFrame::onAutomatonAnt()
{
    setAutomaton(core::Automaton::LangtonAnt);
}

void MainFrame::onAutomatonChanged()
{
    setAutomaton(m_panel->selectedAutomaton());
}

void MainFrame::onResetAnts()
{
    m_world.resetAnts(m_panel->antCount());
    syncControls();
    worldContentChanged();
}

void MainFrame::onEngineBanded()
{
    setEngine(core::StepperKind::Banded);
}

void MainFrame::onEngineReference()
{
    // The menu item is disabled for larger worlds; this check keeps a stray event harmless.
    if (m_world.extent().cellCount() <= core::ReferenceStepper::kRecommendedMaxCells)
    {
        setEngine(core::StepperKind::Reference);
    }
    else
    {
        syncControls();  // restores the radio mark
    }
}

// The canvas reports every camera change through onViewChanged().
void MainFrame::onZoomIn()
{
    m_canvas->zoomBy(1);
}

void MainFrame::onZoomOut()
{
    m_canvas->zoomBy(-1);
}

void MainFrame::onZoomFit()
{
    m_canvas->fitWorld();
}

void MainFrame::onCenterView()
{
    m_canvas->centerWorld();
}

void MainFrame::onCellSizeChanged()
{
    m_canvas->setCellSize(m_panel->cellSize());
}

void MainFrame::onToggleGrid()
{
    m_canvas->setShowGrid(!m_canvas->showGrid());
    syncControls();
    updateStatusBar(true);
}

void MainFrame::onWorldSize()
{
    const bool wasRunning = m_runner.isRunning();
    m_runner.stop();
    m_canvas->cancelStroke();

    const auto request =
        WorldSizeDialog::ask(this, m_world.extent(), m_canvas->cellsThatFit(), m_memoryBudget);
    // The dialog accepts only valid sizes; checking again keeps the budget safe whatever the
    // dialog does.
    if (request && core::validateExtent(request->extent, m_memoryBudget))
    {
        try
        {
            const wxBusyCursor busy;
            m_world.resize(request->extent, request->keepPattern);
            m_canvas->worldExtentChanged();  // at once, so no paint sees the old extent
        }
        catch (const std::bad_alloc&)
        {
            // World::resize() has the strong guarantee: the old world is intact.
            const std::string message = std::format(
                "There is not enough memory for a {} × {} world. The current world was kept.",
                countText(request->extent.width), countText(request->extent.height));
            wxMessageBox(toWx(message), "World Size", wxOK | wxICON_ERROR, this);
        }
        if (m_world.stepper().kind() == core::StepperKind::Reference &&
            m_world.extent().cellCount() > core::ReferenceStepper::kRecommendedMaxCells)
        {
            setEngine(core::StepperKind::Banded);
        }
    }

    if (wasRunning)
    {
        m_runner.start();
    }
    syncControls();
    updateStatusBar(true);
}

void MainFrame::onToggleWrap()
{
    const bool torus = m_world.topology() == core::Topology::Torus;
    m_world.setTopology(torus ? core::Topology::Bounded : core::Topology::Torus);
    syncControls();
    updateStatusBar(true);
}

void MainFrame::onApplyRule()
{
    const auto rule = core::Rule::parse(m_panel->ruleText());
    if (!rule)
    {
        m_panel->setRuleError(core::describe(rule.error()));  // the current rule stays
        return;
    }
    applyRule(*rule);
}

void MainFrame::onRulePreset()
{
    if (const auto preset = m_panel->selectedPreset())  // "Custom" does nothing
    {
        applyRule(core::kRulePresets.at(*preset).rule);
    }
}

void MainFrame::onFocusRule()
{
    if (m_world.automaton() != core::Automaton::Life)
    {
        return;  // the Rule group is greyed out, so there is nothing to focus
    }
    m_panel->focusRuleText();
}

void MainFrame::onShowControlsHelp()
{
    wxMessageBox(toWx(kControlsHelp), "Keyboard and Mouse", wxOK | wxICON_INFORMATION, this);
}

void MainFrame::onAbout()
{
    wxAboutDialogInfo info;
    info.SetName("wxLife");
    info.SetVersion(WXLIFE_VERSION);
    info.SetDescription("Conway's Game of Life and other B/S rules, built with " +
                        wxGetLibraryVersionInfo().GetVersionString() + ".");
    wxAboutBox(info, this);
}

void MainFrame::onClose(wxCloseEvent& event)
{
    m_runner.stop();
    // wx asserts if a window is destroyed while it holds the mouse capture.
    m_canvas->cancelStroke();
    event.Skip();  // wx destroys the frame and its children
    // There is deliberately no destructor calling DestroyChildren(): the menubar is a child
    // window whose deletion does not clear the frame's pointer to it, so it would be deleted
    // twice.
}

void MainFrame::onSimulationTick(const TickReport& /*report*/)
{
    m_canvas->Refresh(false);
    updateStatusBar(false);
}

void MainFrame::onPaintCells(std::span<const core::CellPos> cells, core::Cell value)
{
    if (m_world.setCells(cells, value) > 0)
    {
        worldContentChanged();
    }
}

void MainFrame::onToggleAnt(core::CellPos cell)
{
    if (m_world.automaton() != core::Automaton::LangtonAnt)
    {
        return;  // Ctrl+click means nothing to Life
    }
    m_world.toggleAntAt(cell);
    syncControls();  // the panel's ant count follows the model
    worldContentChanged();
}

void MainFrame::onViewChanged()
{
    m_panel->setCellSize(m_canvas->cellSize());
    updateStatusBar(true);
}

void MainFrame::onHoverChanged(std::optional<core::CellPos> cell)
{
    m_hovered = cell;
    updateStatusBar(true);
}

void MainFrame::applyRule(const core::Rule& rule)
{
    m_world.setRule(rule);
    m_panel->setRule(rule);
    updateStatusBar(true);
}

void MainFrame::setAutomaton(core::Automaton automaton)
{
    m_world.setAutomaton(automaton);
    syncControls();
    worldContentChanged();  // the ants appear or disappear, so the canvas has to be redrawn
}

void MainFrame::setEngine(core::StepperKind kind)
{
    if (m_world.stepper().kind() != kind)
    {
        m_world.setStepper(core::makeStepper(kind));
    }
    syncControls();
    updateStatusBar(true);
}

void MainFrame::worldContentChanged()
{
    m_canvas->Refresh(false);
    updateStatusBar(true);
}

void MainFrame::syncControls()
{
    const bool         running = m_runner.isRunning();
    const core::Speed  speed   = m_runner.speed();
    const core::Extent extent  = m_world.extent();
    const bool         torus   = m_world.topology() == core::Topology::Torus;
    // Life reads the rule, the topology and the engine. The ant reads none of them, and has ants
    // instead.
    const bool life = m_world.automaton() == core::Automaton::Life;

    // The rule text is left alone, so text the user is still editing survives.
    m_panel->setRunning(running);
    m_panel->setAutomaton(m_world.automaton());
    m_panel->setAntCount(static_cast<int>(m_world.ants().size()));
    m_panel->setSpeed(speed);
    m_panel->setCellSize(m_canvas->cellSize());
    m_panel->setShowGrid(m_canvas->showGrid());
    m_panel->setWrap(torus);
    m_panel->setWorldInfo(extent, core::worldBytes(extent));

    wxMenuBar& menus = *GetMenuBar();
    menus.Check(ToggleMaxSpeedID, speed.unlimited);
    menus.Check(ToggleGridID, m_canvas->showGrid());
    menus.Check(ToggleWrapID, torus);
    menus.Check(automatonMenuItem(m_world.automaton()), true);  // radio items: the others turn off
    menus.Check(engineMenuItem(m_world.stepper().kind()), true);
    menus.Enable(EngineBandedID, life);
    menus.Enable(EngineReferenceID,
                 life && extent.cellCount() <= core::ReferenceStepper::kRecommendedMaxCells);
    menus.Enable(ToggleWrapID, life);
    menus.Enable(FocusRuleID, life);
    menus.Enable(ResetAntsID, !life);
    menus.SetLabel(RunPauseID, running ? "&Pause\tF5" : "&Run\tF5");
}

void MainFrame::updateStatusBar(bool force)
{
    const core::Clock::time_point now = core::Clock::now();
    if (!force && now - m_lastStatusUpdate < defaults::kStatusRefresh)
    {
        return;
    }
    m_lastStatusUpdate = now;

    const bool        running  = m_runner.isRunning();
    const core::Speed speed    = m_runner.speed();
    const int         cellSize = m_canvas->cellSize();

    // Only the target until a rate has been measured.
    std::string speedText = core::toString(speed);
    if (const std::optional<double> rate = m_runner.measuredRate(); running && rate)
    {
        speedText = speed.unlimited ? std::format("Max ({} gen/s)", countText(std::llround(*rate)))
                                    : std::format("{} ({:.1f})", speedText, *rate);
    }

    std::string viewText = m_hovered ? std::format("({}, {})", m_hovered->x, m_hovered->y) : "–";
    viewText += std::format(" · {} px", cellSize);
    const render::RenderStyle& style = m_canvas->style();
    if (style.showGrid && !style.gridVisibleAt(cellSize))
    {
        viewText += std::format(" · grid hidden < {} px", style.minCellSizeForGrid);
    }

    const std::array<std::string, kStatusWidths.size()> fields{
        running ? "Running" : "Paused",
        std::format("Gen {}", core::formatCount(m_world.generation())),
        std::format("Pop {}", countText(m_world.population())),
        speedText,
        worldText(m_world),
        viewText,
    };
    // SetStatusText() ignores unchanged text, so rewriting every field is cheap.
    for (int field = 0; const std::string& text : fields)
    {
        SetStatusText(toWx(text), field++);
    }
}

}  // namespace wxLife::ui

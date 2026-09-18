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

// wx's generic status bar repaints the window synchronously (Update()) after every text change.
// Under X11 that paint can come before GTK has laid out a slider moved in the same handler, and
// the slider then keeps showing its old position. A plain refresh lets GTK lay everything out
// first.
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
            return ID_ENGINE_BANDED;
        case core::StepperKind::Reference:
            return ID_ENGINE_REFERENCE;
    }
    std::unreachable();
}

// The same for the Automaton menu, and likewise without a default.
[[nodiscard]] constexpr CommandId automatonMenuItem(core::Automaton automaton) noexcept
{
    switch (automaton)
    {
        case core::Automaton::Life:
            return ID_AUTOMATON_LIFE;
        case core::Automaton::LangtonAnt:
            return ID_AUTOMATON_ANT;
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
    world_(world),
    runner_(world, [this](const TickReport& report) { onSimulationTick(report); })
{
    SetMenuBar(buildMenuBar());
    auto* statusBar = new StatusBar(this);
    statusBar->SetFieldsCount(static_cast<int>(kStatusWidths.size()), kStatusWidths.data());
    SetStatusBar(statusBar);
    // No menu help: the items have none, and wx would blank "Running" to show it.
    SetStatusBarPane(-1);
    buildLayout();
    bindCommands();

    runner_.setSpeed(defaults::kSpeed);
    world_.randomize(panel_->randomDensity(), freshSeed());
    panel_->setRule(world_.rule());
    syncControls();
    updateStatusBar(true);

    SetMinSize(FromDIP(wxSize(defaults::kMinFrameWidthDip, defaults::kMinFrameHeightDip)));
    SetSize(FromDIP(wxSize(defaults::kFrameWidthDip, defaults::kFrameHeightDip)));
    Centre();
    // The world stays fitted until the user moves the camera, so it follows the frame's real size.
    canvas_->fitWorld();
    canvas_->SetFocus();
}

void MainFrame::buildLayout()
{
    panel_ = new ControlPanel(this);
    panel_->SetMinSize(FromDIP(wxSize(defaults::kControlPanelWidthDip, -1)));
    canvas_ = new WorldCanvas(
        this, world_,
        {
            .paintCells   = [this](std::span<const core::CellPos> cells,
                                   core::Cell value) { onPaintCells(cells, value); },
            .toggleAnt    = [this](core::CellPos cell) { onToggleAnt(cell); },
            .viewChanged  = [this] { onViewChanged(); },
            .hoverChanged = [this](std::optional<core::CellPos> cell) { onHoverChanged(cell); },
        });

    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(panel_, wxSizerFlags(0).Expand());
    row->Add(canvas_, wxSizerFlags(1).Expand());
    SetSizer(row);
}

void MainFrame::bindCommands()
{
    using Handler                = void (MainFrame::*)();
    static constexpr auto kTable = std::to_array<std::pair<CommandId, Handler>>({
        {ID_RUN_PAUSE, &MainFrame::onRunPause},
        {ID_STEP, &MainFrame::onStep},
        {ID_CLEAR, &MainFrame::onClear},
        {ID_RANDOMIZE, &MainFrame::onRandomize},
        {ID_FASTER, &MainFrame::onFaster},
        {ID_SLOWER, &MainFrame::onSlower},
        {ID_TOGGLE_MAX_SPEED, &MainFrame::onToggleMaxSpeed},
        {ID_SPEED_CHANGED, &MainFrame::onSpeedChanged},
        {ID_AUTOMATON_LIFE, &MainFrame::onAutomatonLife},
        {ID_AUTOMATON_ANT, &MainFrame::onAutomatonAnt},
        {ID_AUTOMATON_CHANGED, &MainFrame::onAutomatonChanged},
        {ID_RESET_ANTS, &MainFrame::onResetAnts},
        {ID_ENGINE_BANDED, &MainFrame::onEngineBanded},
        {ID_ENGINE_REFERENCE, &MainFrame::onEngineReference},
        {ID_ZOOM_IN, &MainFrame::onZoomIn},
        {ID_ZOOM_OUT, &MainFrame::onZoomOut},
        {ID_ZOOM_FIT, &MainFrame::onZoomFit},
        {ID_CENTER_VIEW, &MainFrame::onCenterView},
        {ID_CELL_SIZE_CHANGED, &MainFrame::onCellSizeChanged},
        {ID_TOGGLE_GRID, &MainFrame::onToggleGrid},
        {ID_WORLD_SIZE, &MainFrame::onWorldSize},
        {ID_TOGGLE_WRAP, &MainFrame::onToggleWrap},
        {ID_APPLY_RULE, &MainFrame::onApplyRule},
        {ID_RULE_PRESET, &MainFrame::onRulePreset},
        {ID_FOCUS_RULE, &MainFrame::onFocusRule},
        {ID_SHOW_CONTROLS_HELP, &MainFrame::onShowControlsHelp},
    });
    for (const auto& [id, handler] : kTable)
    {
        const auto run = [this, id, handler](wxCommandEvent& event) {
            (this->*handler)();
            // GTK leaves the focus on a clicked button or check box, and Space would press it
            // again. The canvas takes the focus back, except after Apply, so that a rejected rule
            // can be fixed.
            const wxObject* source = event.GetEventObject();
            if (id != ID_APPLY_RULE &&
                (dynamic_cast<const wxButton*>(source) || dynamic_cast<const wxCheckBox*>(source)))
            {
                canvas_->SetFocus();
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
            emitCommand(*this, ID_TOGGLE_MAX_SPEED);
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
    runner_.toggle();
    syncControls();
    updateStatusBar(true);
}

void MainFrame::onStep()
{
    runner_.stepOnce();  // reports through onSimulationTick()
    syncControls();
    updateStatusBar(true);  // the tick's own update may have been throttled
}

void MainFrame::onClear()
{
    world_.clear();
    worldContentChanged();
}

void MainFrame::onRandomize()
{
    world_.randomize(panel_->randomDensity(), freshSeed());
    worldContentChanged();
}

void MainFrame::onFaster()
{
    runner_.setSpeed(runner_.speed().faster());
    syncControls();
    updateStatusBar(true);
}

void MainFrame::onSlower()
{
    runner_.setSpeed(runner_.speed().slower());
    syncControls();
    updateStatusBar(true);
}

void MainFrame::onToggleMaxSpeed()
{
    core::Speed speed = runner_.speed();

    speed.unlimited = !speed.unlimited;
    runner_.setSpeed(speed);
    syncControls();
    updateStatusBar(true);
}

void MainFrame::onSpeedChanged()
{
    runner_.setSpeed(panel_->speed());
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
    setAutomaton(panel_->selectedAutomaton());
}

void MainFrame::onResetAnts()
{
    world_.resetAnts(panel_->antCount());
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
    if (world_.extent().cellCount() <= core::ReferenceStepper::kRecommendedMaxCells)
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
    canvas_->zoomBy(1);
}

void MainFrame::onZoomOut()
{
    canvas_->zoomBy(-1);
}

void MainFrame::onZoomFit()
{
    canvas_->fitWorld();
}

void MainFrame::onCenterView()
{
    canvas_->centerWorld();
}

void MainFrame::onCellSizeChanged()
{
    canvas_->setCellSize(panel_->cellSize());
}

void MainFrame::onToggleGrid()
{
    canvas_->setShowGrid(!canvas_->showGrid());
    syncControls();
    updateStatusBar(true);
}

void MainFrame::onWorldSize()
{
    const bool wasRunning = runner_.isRunning();
    runner_.stop();
    canvas_->cancelStroke();

    const auto request =
        WorldSizeDialog::ask(this, world_.extent(), canvas_->cellsThatFit(), memoryBudget_);
    // The dialog accepts only valid sizes; checking again keeps the budget safe whatever the
    // dialog does.
    if (request && core::validateExtent(request->extent, memoryBudget_))
    {
        try
        {
            const wxBusyCursor busy;
            world_.resize(request->extent, request->keepPattern);
            canvas_->worldExtentChanged();  // at once, so no paint sees the old extent
        }
        catch (const std::bad_alloc&)
        {
            // World::resize() has the strong guarantee: the old world is intact.
            const std::string message = std::format(
                "There is not enough memory for a {} × {} world. The current world was kept.",
                countText(request->extent.width), countText(request->extent.height));
            wxMessageBox(toWx(message), "World Size", wxOK | wxICON_ERROR, this);
        }
        if (world_.stepper().kind() == core::StepperKind::Reference &&
            world_.extent().cellCount() > core::ReferenceStepper::kRecommendedMaxCells)
        {
            setEngine(core::StepperKind::Banded);
        }
    }

    if (wasRunning)
    {
        runner_.start();
    }
    syncControls();
    updateStatusBar(true);
}

void MainFrame::onToggleWrap()
{
    const bool torus = world_.topology() == core::Topology::Torus;
    world_.setTopology(torus ? core::Topology::Bounded : core::Topology::Torus);
    syncControls();
    updateStatusBar(true);
}

void MainFrame::onApplyRule()
{
    const auto rule = core::Rule::parse(panel_->ruleText());
    if (!rule)
    {
        panel_->setRuleError(core::describe(rule.error()));  // the current rule stays
        return;
    }
    applyRule(*rule);
}

void MainFrame::onRulePreset()
{
    if (const auto preset = panel_->selectedPreset())  // "Custom" does nothing
    {
        applyRule(core::kRulePresets.at(*preset).rule);
    }
}

void MainFrame::onFocusRule()
{
    if (world_.automaton() != core::Automaton::Life)
    {
        return;  // the Rule group is greyed out, so there is nothing to focus
    }
    panel_->focusRuleText();
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
    runner_.stop();
    // wx asserts if a window is destroyed while it holds the mouse capture.
    canvas_->cancelStroke();
    event.Skip();  // wx destroys the frame and its children
    // There is deliberately no destructor calling DestroyChildren(): the menubar is a child
    // window whose deletion does not clear the frame's pointer to it, so it would be deleted
    // twice.
}

void MainFrame::onSimulationTick(const TickReport& /*report*/)
{
    canvas_->Refresh(false);
    updateStatusBar(false);
}

void MainFrame::onPaintCells(std::span<const core::CellPos> cells, core::Cell value)
{
    if (world_.setCells(cells, value) > 0)
    {
        worldContentChanged();
    }
}

void MainFrame::onToggleAnt(core::CellPos cell)
{
    if (world_.automaton() != core::Automaton::LangtonAnt)
    {
        return;  // Ctrl+click means nothing to Life
    }
    world_.toggleAntAt(cell);
    syncControls();  // the panel's ant count follows the model
    worldContentChanged();
}

void MainFrame::onViewChanged()
{
    panel_->setCellSize(canvas_->cellSize());
    updateStatusBar(true);
}

void MainFrame::onHoverChanged(std::optional<core::CellPos> cell)
{
    hovered_ = cell;
    updateStatusBar(true);
}

void MainFrame::applyRule(const core::Rule& rule)
{
    world_.setRule(rule);
    panel_->setRule(rule);
    updateStatusBar(true);
}

void MainFrame::setAutomaton(core::Automaton automaton)
{
    world_.setAutomaton(automaton);
    syncControls();
    worldContentChanged();  // the ants appear or disappear, so the canvas has to be redrawn
}

void MainFrame::setEngine(core::StepperKind kind)
{
    if (world_.stepper().kind() != kind)
    {
        world_.setStepper(core::makeStepper(kind));
    }
    syncControls();
    updateStatusBar(true);
}

void MainFrame::worldContentChanged()
{
    canvas_->Refresh(false);
    updateStatusBar(true);
}

void MainFrame::syncControls()
{
    const bool         running = runner_.isRunning();
    const core::Speed  speed   = runner_.speed();
    const core::Extent extent  = world_.extent();
    const bool         torus   = world_.topology() == core::Topology::Torus;
    // Life reads the rule, the topology and the engine. The ant reads none of them, and has ants
    // instead.
    const bool life = world_.automaton() == core::Automaton::Life;

    // The rule text is left alone, so text the user is still editing survives.
    panel_->setRunning(running);
    panel_->setAutomaton(world_.automaton());
    panel_->setAntCount(static_cast<int>(world_.ants().size()));
    panel_->setSpeed(speed);
    panel_->setCellSize(canvas_->cellSize());
    panel_->setShowGrid(canvas_->showGrid());
    panel_->setWrap(torus);
    panel_->setWorldInfo(extent, core::worldBytes(extent));

    wxMenuBar& menus = *GetMenuBar();
    menus.Check(ID_TOGGLE_MAX_SPEED, speed.unlimited);
    menus.Check(ID_TOGGLE_GRID, canvas_->showGrid());
    menus.Check(ID_TOGGLE_WRAP, torus);
    menus.Check(automatonMenuItem(world_.automaton()), true);  // radio items: the others turn off
    menus.Check(engineMenuItem(world_.stepper().kind()), true);
    menus.Enable(ID_ENGINE_BANDED, life);
    menus.Enable(ID_ENGINE_REFERENCE,
                 life && extent.cellCount() <= core::ReferenceStepper::kRecommendedMaxCells);
    menus.Enable(ID_TOGGLE_WRAP, life);
    menus.Enable(ID_FOCUS_RULE, life);
    menus.Enable(ID_RESET_ANTS, !life);
    menus.SetLabel(ID_RUN_PAUSE, running ? "&Pause\tF5" : "&Run\tF5");
}

void MainFrame::updateStatusBar(bool force)
{
    const core::Clock::time_point now = core::Clock::now();
    if (!force && now - lastStatusUpdate_ < defaults::kStatusRefresh)
    {
        return;
    }
    lastStatusUpdate_ = now;

    const bool        running  = runner_.isRunning();
    const core::Speed speed    = runner_.speed();
    const int         cellSize = canvas_->cellSize();

    // Only the target until a rate has been measured.
    std::string speedText = core::toString(speed);
    if (const std::optional<double> rate = runner_.measuredRate(); running && rate)
    {
        speedText = speed.unlimited ? std::format("Max ({} gen/s)", countText(std::llround(*rate)))
                                    : std::format("{} ({:.1f})", speedText, *rate);
    }

    std::string viewText = hovered_ ? std::format("({}, {})", hovered_->x, hovered_->y) : "–";
    viewText += std::format(" · {} px", cellSize);
    const render::RenderStyle& style = canvas_->style();
    if (style.showGrid && !style.gridVisibleAt(cellSize))
    {
        viewText += std::format(" · grid hidden < {} px", style.minCellSizeForGrid);
    }

    const std::array<std::string, kStatusWidths.size()> fields{
        running ? "Running" : "Paused",
        std::format("Gen {}", core::formatCount(world_.generation())),
        std::format("Pop {}", countText(world_.population())),
        speedText,
        worldText(world_),
        viewText,
    };
    // SetStatusText() ignores unchanged text, so rewriting every field is cheap.
    for (int field = 0; const std::string& text : fields)
    {
        SetStatusText(toWx(text), field++);
    }
}

}  // namespace wxLife::ui

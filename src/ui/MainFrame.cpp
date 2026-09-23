#include "wxLife/ui/MainFrame.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
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
#include <wx/ffile.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statusbr.h>
#include <wx/utils.h>

#include "wxLife/core/Demo.h"
#include "wxLife/core/Format.h"
#include "wxLife/core/HashLife.h"
#include "wxLife/core/Pacer.h"
#include "wxLife/core/Pattern.h"
#include "wxLife/core/PatternSetup.h"
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
#include "wxLife/ui/DemoDialog.h"
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
    ] and [: faster and slower    } and {: larger and smaller step
    + and -: zoom in and out
    F: fit the world    C or Home: center it
    G: grid lines    W: wrap edges
    Arrows: pan 10% (Shift: 90%)    Page Up/Down: pan 90%
    Esc: end a stroke

Everywhere (a focused text or number box keeps its own editing
keys, such as Ctrl+Home and Ctrl+Delete)
    F5: run or pause    F6: step
    Ctrl+] and Ctrl+[: faster and slower    Ctrl+M: max speed
    F8 and F7: larger and smaller step (unbounded worlds)
    Ctrl+O: open a pattern file    Ctrl+D: demo patterns
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

[[nodiscard]] std::string sizeText(core::Extent extent)
{
    return std::format("{} × {}", countText(extent.width), countText(extent.height));
}

/// Pattern files larger than this are not read: no pattern that fits a world is anywhere near it.
constexpr std::uint64_t kMaxPatternFileBytes = std::uint64_t{256} << 20;

// The whole file, or a message that says why it cannot be read.
[[nodiscard]] std::expected<std::string, std::string> readPatternFile(const wxString& path)
{
    const wxLogNull    quiet;  // the message box below says what went wrong, not wx's log
    wxFFile            file(path, "rb");
    const wxFileOffset length = file.IsOpened() ? file.Length() : wxInvalidOffset;
    if (length < 0)
    {
        return std::unexpected(std::string("The file cannot be read."));
    }
    if (std::cmp_greater(length, kMaxPatternFileBytes))
    {
        return std::unexpected(std::format("The file is {}; pattern files over {} are not read.",
                                           core::formatBytes(static_cast<std::uint64_t>(length)),
                                           core::formatBytes(kMaxPatternFileBytes)));
    }
    std::string text(static_cast<std::size_t>(length), '\0');
    if (file.Read(text.data(), text.size()) != text.size())
    {
        return std::unexpected(std::string("The file cannot be read."));
    }
    return text;
}

// The Engine menu item of each engine. There is no default, so -Wswitch points here when an
// engine is added.
[[nodiscard]] constexpr CommandId engineMenuItem(core::StepperKind kind) noexcept
{
    switch (kind)
    {
        case core::StepperKind::BANDED:
            return ID_ENGINE_BANDED;
        case core::StepperKind::REFERENCE:
            return ID_ENGINE_REFERENCE;
    }
    std::unreachable();
}

// The same for the Automaton menu, and likewise without a default.
[[nodiscard]] constexpr CommandId automatonMenuItem(core::Automaton automaton) noexcept
{
    switch (automaton)
    {
        case core::Automaton::LIFE:
            return ID_AUTOMATON_LIFE;
        case core::Automaton::LANGTON_ANT:
            return ID_AUTOMATON_ANT;
    }
    std::unreachable();
}

// The world field of the status bar: what the running automaton actually uses. Life's text is
// also what the GUI smoke test pins, so it must stay exactly as it is.
[[nodiscard]] std::string worldText(const core::World& world)
{
    if (world.kind() == core::WorldKind::UNBOUNDED)
    {
        return std::format("Unbounded · {} · HashLife", world.rule().toString());
    }
    const core::Extent extent = world.extent();
    const std::string  size =
        std::format("{} × {}", countText(extent.width), countText(extent.height));
    switch (world.automaton())
    {
        case core::Automaton::LIFE:
            return std::format("{} · {} · {} · {}", size, core::toString(world.topology()),
                               world.rule().toString(), core::toString(world.stepper().kind()));
        case core::Automaton::LANGTON_ANT:
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
            .paintCells   = [this](std::span<const core::UniversePos> cells,
                                   core::Cell value) { onPaintCells(cells, value); },
            .toggleAnt    = [this](core::UniversePos cell) { onToggleAnt(cell); },
            .viewChanged  = [this] { onViewChanged(); },
            .hoverChanged = [this](std::optional<core::UniversePos> cell) { onHoverChanged(cell); },
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
        {ID_RUN_PAUSE, &MainFrame::onRunPause},
        {ID_STEP, &MainFrame::onStep},
        {ID_CLEAR, &MainFrame::onClear},
        {ID_RANDOMIZE, &MainFrame::onRandomize},
        {ID_OPEN_PATTERN, &MainFrame::onOpenPattern},
        {ID_DEMO_PATTERNS, &MainFrame::onDemoPatterns},
        {ID_FASTER, &MainFrame::onFaster},
        {ID_SLOWER, &MainFrame::onSlower},
        {ID_TOGGLE_MAX_SPEED, &MainFrame::onToggleMaxSpeed},
        {ID_SPEED_CHANGED, &MainFrame::onSpeedChanged},
        {ID_LARGER_STEP, &MainFrame::onLargerStep},
        {ID_SMALLER_STEP, &MainFrame::onSmallerStep},
        {ID_STEP_SIZE_CHANGED, &MainFrame::onStepSizeChanged},
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
    m_runner.interrupt();
    m_world.clear();
    worldContentChanged();
}

void MainFrame::onRandomize()
{
    m_runner.interrupt();
    if (m_world.kind() == core::WorldKind::FIXED_SIZE)
    {
        m_world.randomize(m_panel->randomDensity(), freshSeed());
        worldContentChanged();
        return;
    }
    // A plane has no whole to fill, so what the view shows is filled.
    core::UniverseRect area = m_canvas->visibleCells();
    area.x1                 = std::min(area.x1, area.x0 + core::kMaxWorldSide);
    area.y1                 = std::min(area.y1, area.y0 + core::kMaxWorldSide);
    try
    {
        const wxBusyCursor busy;
        m_world.randomize(m_panel->randomDensity(), freshSeed(), area);
    }
    catch (const std::bad_alloc&)
    {
        wxMessageBox("There is not enough memory for that many cells. The world was cleared.",
                     "Randomize", wxOK | wxICON_ERROR, this);
        m_world.clear();
    }
    worldContentChanged();
}

void MainFrame::onOpenPattern()
{
    const bool wasRunning = m_runner.isRunning();
    m_runner.stop();
    m_canvas->cancelStroke();

    wxFileDialog dialog(this, "Open Pattern", m_lastPatternDir, wxString(),
                        "Pattern files (*.rle;*.cells)|*.rle;*.cells|All files|*",
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK && openPatternFile(dialog.GetPath()))
    {
        return;  // paused at generation 0
    }
    // Cancelled, or the file could not be used: everything goes on as before.
    if (wasRunning)
    {
        m_runner.start();
    }
    syncControls();
    updateStatusBar(true);
}

bool MainFrame::openPatternFile(const wxString& path)
{
    m_lastPatternDir                                   = wxFileName(path).GetPath();
    const std::expected<std::string, std::string> text = readPatternFile(path);
    const auto pattern = text.and_then([](const std::string& contents) {
        return core::readPattern(contents).transform_error(
            [](const core::PatternError& error) { return core::describe(error); });
    });
    const auto setup   = pattern.and_then([this](const core::Pattern& read) {
        return core::fileSetup(read, m_world.rule(), m_world.kind(), m_world.topology(),
                               m_memoryBudget)
            .transform_error([&](core::ExtentError error) {
                return std::format("The pattern is {} cells. {}", sizeText(read.extent),
                                   core::describe(error, read.extent, m_memoryBudget));
            })
            .and_then([](const core::PatternSetup& ready)
                          -> std::expected<core::PatternSetup, std::string> {
                if (ready.kind == core::WorldKind::UNBOUNDED &&
                    !core::HashLife::supports(ready.rule))
                {
                    return std::unexpected(std::format(
                        "It runs {}, and an unbounded world cannot run a rule with B0: every "
                        "empty cell of the plane would be born at once.",
                        ready.rule.toString()));
                }
                return ready;
            });
    });
    if (!setup)
    {
        const std::string message = std::format(
            "Cannot open {}.\n\n{}", toUtf8(wxFileName(path).GetFullName()), setup.error());
        wxMessageBox(toWx(message), "Open Pattern", wxOK | wxICON_ERROR, this);
        return false;
    }
    loadPattern(*pattern, *setup);
    return true;
}

void MainFrame::onDemoPatterns()
{
    const bool wasRunning = m_runner.isRunning();
    m_runner.stop();
    m_canvas->cancelStroke();

    const std::optional<std::size_t> chosen = DemoDialog::ask(this, m_lastDemo, m_memoryBudget);
    if (!chosen)
    {
        if (wasRunning)
        {
            m_runner.start();
        }
        syncControls();
        updateStatusBar(true);
        return;
    }
    m_lastDemo                  = chosen;
    const core::Demo&   demo    = core::demos()[*chosen];
    const core::Pattern pattern = core::demoPattern(demo);
    loadPattern(pattern, core::demoSetup(demo, pattern));
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

void MainFrame::onLargerStep()
{
    setStepExponent(m_runner.stepExponent() + 1);
}

void MainFrame::onSmallerStep()
{
    if (m_runner.stepExponent() > 0)
    {
        setStepExponent(m_runner.stepExponent() - 1);
    }
}

void MainFrame::onStepSizeChanged()
{
    setStepExponent(m_panel->stepExponent());
}

void MainFrame::onAutomatonLife()
{
    setAutomaton(core::Automaton::LIFE);
}

void MainFrame::onAutomatonAnt()
{
    setAutomaton(core::Automaton::LANGTON_ANT);
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
    if (m_world.kind() == core::WorldKind::FIXED_SIZE)
    {
        setEngine(core::StepperKind::BANDED);
    }
    else
    {
        syncControls();  // an unbounded world runs HashLife; the menu item is disabled anyway
    }
}

void MainFrame::onEngineReference()
{
    // The menu item is disabled for larger and unbounded worlds; this check keeps a stray event
    // harmless.
    if (m_world.kind() == core::WorldKind::FIXED_SIZE &&
        m_world.extent().cellCount() <= core::ReferenceStepper::kRecommendedMaxCells)
    {
        setEngine(core::StepperKind::REFERENCE);
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

    const std::string refusal = unboundedRefusal();
    const auto request = WorldSizeDialog::ask(this, m_world.kind(), m_world.extent(),
                                              m_canvas->cellsThatFit(), m_memoryBudget, refusal);
    // The dialog accepts only valid choices; checking again keeps the budget safe whatever the
    // dialog does.
    const bool valid =
        request && (request->kind == core::WorldKind::UNBOUNDED
                        ? refusal.empty()
                        : core::validateExtent(request->extent, m_memoryBudget).has_value());
    if (valid)
    {
        try
        {
            const wxBusyCursor busy;
            if (request->kind == core::WorldKind::UNBOUNDED)
            {
                m_world.makeUnbounded(request->keepPattern, m_memoryBudget);
            }
            else
            {
                m_world.resize(request->extent, request->keepPattern);
            }
            m_canvas->worldExtentChanged();  // at once, so no paint sees the old extent
        }
        catch (const std::bad_alloc&)
        {
            // Both changes have the strong guarantee: the old world is intact.
            const std::string message =
                request->kind == core::WorldKind::UNBOUNDED
                    ? std::string(
                          "There is not enough memory for an unbounded world with this "
                          "pattern. The current world was kept.")
                    : std::format(
                          "There is not enough memory for a {} world. The current world "
                          "was kept.",
                          sizeText(request->extent));
            wxMessageBox(toWx(message), "World Size", wxOK | wxICON_ERROR, this);
        }
        if (m_world.kind() == core::WorldKind::FIXED_SIZE &&
            m_world.stepper().kind() == core::StepperKind::REFERENCE &&
            m_world.extent().cellCount() > core::ReferenceStepper::kRecommendedMaxCells)
        {
            setEngine(core::StepperKind::BANDED);
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
    if (m_world.kind() == core::WorldKind::FIXED_SIZE)  // a plane has no edges to wrap
    {
        const bool torus = m_world.topology() == core::Topology::TORUS;
        m_world.setTopology(torus ? core::Topology::BOUNDED : core::Topology::TORUS);
    }
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
    if (m_world.automaton() != core::Automaton::LIFE)
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

void MainFrame::onSimulationTick(const TickReport& report)
{
    if (report.error)
    {
        syncControls();
        updateStatusBar(true);
        wxMessageBox(toWx(core::describe(*report.error)), "Simulation Stopped",
                     wxOK | wxICON_WARNING, this);
        return;
    }
    m_canvas->Refresh(false);
    // A busy report, or the end of a single step, must show at once; running ticks are throttled.
    updateStatusBar(report.generationsStepped == 0 || !m_runner.isRunning());
}

void MainFrame::onPaintCells(std::span<const core::UniversePos> cells, core::Cell value)
{
    m_runner.interrupt();  // the running step goes on at the next tick, with the new cells
    try
    {
        if (m_world.setCells(cells, value) > 0)
        {
            worldContentChanged();
        }
    }
    catch (const std::bad_alloc&)
    {
        m_canvas->cancelStroke();
        wxMessageBox("There is not enough memory for more cells.", "Draw", wxOK | wxICON_ERROR,
                     this);
    }
}

void MainFrame::onToggleAnt(core::UniversePos cell)
{
    const core::Extent extent = m_world.extent();
    if (m_world.automaton() != core::Automaton::LANGTON_ANT || cell.x >= extent.width ||
        cell.y >= extent.height || cell.x < 0 || cell.y < 0)
    {
        return;  // Ctrl+click means nothing to Life, and ants live only in fixed-size worlds
    }
    m_world.toggleAntAt(
        {.x = static_cast<core::Coord>(cell.x), .y = static_cast<core::Coord>(cell.y)});
    syncControls();  // the panel's ant count follows the model
    worldContentChanged();
}

void MainFrame::onViewChanged()
{
    m_panel->setCellSize(m_canvas->cellSize());
    updateStatusBar(true);
}

void MainFrame::onHoverChanged(std::optional<core::UniversePos> cell)
{
    m_hovered = cell;
    updateStatusBar(true);
}

void MainFrame::loadPattern(const core::Pattern& pattern, const core::PatternSetup& setup)
{
    m_runner.stop();
    m_canvas->cancelStroke();
    const bool unbounded = setup.kind == core::WorldKind::UNBOUNDED;
    // The callers offer only worlds that fit; checking again keeps the budget safe whatever they
    // do.
    if (const auto valid = core::validateExtent(setup.world, m_memoryBudget); !unbounded && !valid)
    {
        wxMessageBox(toWx(core::describe(valid.error(), setup.world, m_memoryBudget)),
                     "Load Pattern", wxOK | wxICON_ERROR, this);
        syncControls();
        updateStatusBar(true);
        return;
    }
    try
    {
        const wxBusyCursor busy;
        if (unbounded)
        {
            // A plane runs Life with a rule it supports (the caller checked), so both come first.
            m_world.setAutomaton(core::Automaton::LIFE);
            m_world.setRule(setup.rule);
            m_world.makeUnbounded(false, m_memoryBudget);
        }
        else if (m_world.kind() == core::WorldKind::FIXED_SIZE && setup.world == m_world.extent())
        {
            m_world.clear();
        }
        else
        {
            m_world.resize(setup.world, false);
        }
        m_canvas->worldExtentChanged();  // at once, so no paint sees the old extent
    }
    catch (const std::bad_alloc&)
    {
        // World::resize() and makeUnbounded() have the strong guarantee: the old world is intact.
        const std::string message =
            unbounded ? std::string(
                            "There is not enough memory for an unbounded world. The "
                            "current world was kept.")
                      : std::format(
                            "There is not enough memory for a {} world. The current "
                            "world was kept.",
                            sizeText(setup.world));
        wxMessageBox(toWx(message), "Load Pattern", wxOK | wxICON_ERROR, this);
        syncControls();
        updateStatusBar(true);
        return;
    }

    m_world.setTopology(setup.topology);
    applyRule(setup.rule);
    if (setup.automaton == core::Automaton::LANGTON_ANT)
    {
        m_world.setAnts(setup.ants);  // before the switch, which would add an ant to an empty list
    }
    m_world.setAutomaton(setup.automaton);
    try
    {
        m_world.setCells(pattern.cells, core::kAlive, setup.origin);
    }
    catch (const std::bad_alloc&)
    {
        wxMessageBox("There is not enough memory for the whole pattern.", "Load Pattern",
                     wxOK | wxICON_ERROR, this);
    }
    if (setup.speed)
    {
        m_runner.setSpeed(*setup.speed);
    }
    if (m_world.kind() == core::WorldKind::FIXED_SIZE &&
        m_world.stepper().kind() == core::StepperKind::REFERENCE &&
        m_world.extent().cellCount() > core::ReferenceStepper::kRecommendedMaxCells)
    {
        m_world.setStepper(core::makeStepper(core::StepperKind::BANDED));
    }
    if (setup.view)
    {
        m_canvas->showCells({.x0 = setup.view->x0,
                             .y0 = setup.view->y0,
                             .x1 = setup.view->x1,
                             .y1 = setup.view->y1});
    }
    else if (unbounded)
    {
        m_canvas->fitWorld();  // the pattern, now that it is there
    }
    syncControls();
    worldContentChanged();
}

bool MainFrame::applyRule(const core::Rule& rule)
{
    if (m_world.kind() == core::WorldKind::UNBOUNDED && !core::HashLife::supports(rule))
    {
        m_panel->setRuleError(
            "An unbounded world cannot run a rule with B0: every empty cell "
            "of the plane would be born at once.");
        return false;
    }
    m_runner.interrupt();
    m_world.setRule(rule);
    m_panel->setRule(rule);
    updateStatusBar(true);
    return true;
}

std::string MainFrame::unboundedRefusal() const
{
    if (m_world.automaton() != core::Automaton::LIFE)
    {
        return "Langton's ant needs a fixed-size world.";
    }
    if (!core::HashLife::supports(m_world.rule()))
    {
        return std::format(
            "An unbounded world cannot run {}: with B0, every empty cell would be "
            "born at once.",
            m_world.rule().toString());
    }
    return {};
}

void MainFrame::setStepExponent(unsigned exponent)
{
    if (m_world.kind() == core::WorldKind::UNBOUNDED)
    {
        m_runner.setStepExponent(exponent);
    }
    syncControls();
    updateStatusBar(true);
}

void MainFrame::setAutomaton(core::Automaton automaton)
{
    // An unbounded world runs only Life; its menu item and choice are disabled anyway.
    if (m_world.kind() == core::WorldKind::FIXED_SIZE || automaton == core::Automaton::LIFE)
    {
        m_world.setAutomaton(automaton);
    }
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
    const bool         running   = m_runner.isRunning();
    const core::Speed  speed     = m_runner.speed();
    const core::Extent extent    = m_world.extent();
    const bool         torus     = m_world.topology() == core::Topology::TORUS;
    const bool         unbounded = m_world.kind() == core::WorldKind::UNBOUNDED;
    const unsigned     step      = m_runner.stepExponent();
    // Life reads the rule, the topology and the engine. The ant reads none of them, and has ants
    // instead. A plane runs only Life, with HashLife, and has no edges.
    const bool life = m_world.automaton() == core::Automaton::LIFE;

    // The rule text is left alone, so text the user is still editing survives.
    m_panel->setRunning(running);
    m_panel->setWorldKind(m_world.kind());
    m_panel->setAutomaton(m_world.automaton());
    m_panel->setAntCount(static_cast<int>(m_world.ants().size()));
    m_panel->setSpeed(speed);
    m_panel->setStepExponent(step);
    m_panel->setCellSize(m_canvas->cellSize());
    m_panel->setShowGrid(m_canvas->showGrid());
    m_panel->setWrap(torus);
    if (!unbounded)
    {
        m_panel->setWorldInfo(extent, core::worldBytes(extent));
    }
    else if (!m_runner.busyFor())  // the node count is the worker's while a step runs
    {
        m_panel->setUnboundedInfo(m_world.plane().memoryBytes());
    }

    wxMenuBar& menus = *GetMenuBar();
    menus.Check(ID_TOGGLE_MAX_SPEED, speed.unlimited);
    menus.Check(ID_TOGGLE_GRID, m_canvas->showGrid());
    menus.Check(ID_TOGGLE_WRAP, torus);
    menus.Check(automatonMenuItem(m_world.automaton()), true);  // radio items: the others turn off
    menus.Check(engineMenuItem(m_world.stepper().kind()), true);
    menus.Enable(ID_AUTOMATON_ANT, !unbounded);
    menus.Enable(ID_ENGINE_BANDED, life && !unbounded);
    menus.Enable(
        ID_ENGINE_REFERENCE,
        life && !unbounded && extent.cellCount() <= core::ReferenceStepper::kRecommendedMaxCells);
    menus.Enable(ID_TOGGLE_WRAP, life && !unbounded);
    menus.Enable(ID_FOCUS_RULE, life);
    menus.Enable(ID_RESET_ANTS, !life);
    menus.Enable(ID_LARGER_STEP, unbounded && step < SimulationRunner::kMaxStepExponent);
    menus.Enable(ID_SMALLER_STEP, unbounded && step > 0);
    menus.SetLabel(ID_RUN_PAUSE, running ? "&Pause\tF5" : "&Run\tF5");
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
    if (m_world.kind() == core::WorldKind::UNBOUNDED && m_runner.stepExponent() > 0)
    {
        speedText += std::format(" · step 2^{}", m_runner.stepExponent());
    }
    // A long step: the status says so, rather than seeming stuck.
    const std::optional<core::Clock::duration> busy = m_runner.busyFor();
    const bool  computing = busy && *busy >= SimulationRunner::kBusyReport;
    std::string stateText = running ? "Running" : "Paused";
    if (computing)
    {
        stateText = "Computing…";
    }

    std::string viewText = m_hovered ? std::format("({}, {})", m_hovered->x, m_hovered->y) : "–";
    viewText += std::format(" · {} px", cellSize);
    const render::RenderStyle& style = m_canvas->style();
    if (style.showGrid && !style.gridVisibleAt(cellSize))
    {
        viewText += std::format(" · grid hidden < {} px", style.minCellSizeForGrid);
    }

    const std::array<std::string, kStatusWidths.size()> fields{
        stateText,
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

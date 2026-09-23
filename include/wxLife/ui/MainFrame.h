#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include <wx/event.h>
#include <wx/frame.h>
#include <wx/string.h>

#include "wxLife/core/Pacer.h"
#include "wxLife/core/Pattern.h"
#include "wxLife/core/PatternSetup.h"
#include "wxLife/core/Rule.h"
#include "wxLife/core/Stepper.h"
#include "wxLife/core/Types.h"
#include "wxLife/core/World.h"
#include "wxLife/core/WorldLimits.h"
#include "wxLife/ui/SimulationRunner.h"

namespace wxLife::ui
{

class ControlPanel;
class WorldCanvas;

/// Top-level window that maps every CommandId to a handler and keeps the controls in step with the
/// model. Together with SimulationRunner it is the only code that changes the World.
class MainFrame final : public wxFrame
{
public:
    /// `world` is owned by LifeApp and outlives this frame.
    explicit MainFrame(core::World& world);

    /// Loads the RLE or plaintext pattern file at `path` into a world with room around it, as
    /// File → Open does after its dialog, and pauses at generation 0.
    /// @return false, after a message box that says why, if the file cannot be used; the world
    ///         is then unchanged.
    bool openPatternFile(const wxString& path);

private:
    void buildLayout();
    void bindCommands();

    // One handler per CommandId (the table is in bindCommands()).
    void onRunPause();
    void onStep();
    void onClear();
    void onRandomize();
    void onOpenPattern();
    void onDemoPatterns();
    void onFaster();
    void onSlower();
    void onToggleMaxSpeed();
    void onSpeedChanged();
    void onAutomatonLife();
    void onAutomatonAnt();
    void onAutomatonChanged();
    void onResetAnts();
    void onEngineBanded();
    void onEngineReference();
    void onZoomIn();
    void onZoomOut();
    void onZoomFit();
    void onCenterView();
    void onCellSizeChanged();
    void onToggleGrid();
    void onWorldSize();
    void onToggleWrap();
    void onApplyRule();
    void onRulePreset();
    void onFocusRule();
    void onShowControlsHelp();
    void onAbout();
    void onClose(wxCloseEvent& event);

    // Callbacks from the runner and the canvas.
    void onSimulationTick(const TickReport& report);
    void onPaintCells(std::span<const core::CellPos> cells, core::Cell value);
    void onToggleAnt(core::CellPos cell);
    void onViewChanged();
    void onHoverChanged(std::optional<core::CellPos> cell);

    /// Replaces the world with `pattern` as `setup` says: size, edges, rule, automaton, ants, speed
    /// and view. The simulation is paused at generation 0 afterwards. On failure the world is kept
    /// and a message says why.
    void loadPattern(const core::Pattern& pattern, const core::PatternSetup& setup);
    void applyRule(const core::Rule& rule);
    void setAutomaton(core::Automaton automaton);
    void setEngine(core::StepperKind kind);
    /// Refresh the canvas and force a status update after an edit.
    void worldContentChanged();
    /// Panel values, menu check marks, enabled states and labels.
    void syncControls();
    /// Without `force`, at most once per defaults::kStatusRefresh.
    void updateStatusBar(bool force);

    core::World&                 m_world;
    SimulationRunner             m_runner;
    std::uint64_t                m_memoryBudget = core::defaultMemoryBudget();
    WorldCanvas*                 m_canvas{};  ///< Owned by wx.
    ControlPanel*                m_panel{};   ///< Owned by wx.
    std::optional<core::CellPos> m_hovered;
    std::optional<std::size_t>   m_lastDemo;        ///< Index into core::demos(), for the dialog.
    wxString                     m_lastPatternDir;  ///< Where File → Open looks first.
    core::Clock::time_point      m_lastStatusUpdate;
};

}  // namespace wxLife::ui

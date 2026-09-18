#pragma once

#include <wx/defs.h>
#include <wx/window.h>

namespace wxLife::ui
{

/// A command sent by a menu item, a panel control or a canvas key, as a wxEVT_MENU id.
/// MainFrame::bindCommands() is the only table that maps commands to handlers. Camera moves and
/// strokes stay inside WorldCanvas, and Quit and About use wx's stock ids.
/// A plain enum of int on purpose: wx takes window ids as int.
enum CommandId : int  // NOLINT(cppcoreguidelines-use-enum-class,performance-enum-size)
{
    ID_RUN_PAUSE = wxID_HIGHEST + 1,
    ID_STEP,
    ID_CLEAR,
    ID_RANDOMIZE,
    ID_RESET_ANTS,  ///< Read ControlPanel::antCount().
    ID_FASTER,
    ID_SLOWER,
    ID_TOGGLE_MAX_SPEED,
    ID_SPEED_CHANGED,  ///< Read ControlPanel::speed().
    ID_AUTOMATON_LIFE,
    ID_AUTOMATON_ANT,
    ID_AUTOMATON_CHANGED,  ///< Read ControlPanel::selectedAutomaton().
    ID_ENGINE_BANDED,
    ID_ENGINE_REFERENCE,
    ID_ZOOM_IN,
    ID_ZOOM_OUT,
    ID_ZOOM_FIT,
    ID_CENTER_VIEW,
    ID_CELL_SIZE_CHANGED,  ///< Read ControlPanel::cellSize().
    ID_TOGGLE_GRID,
    ID_WORLD_SIZE,
    ID_TOGGLE_WRAP,
    ID_APPLY_RULE,   ///< Read ControlPanel::ruleText().
    ID_RULE_PRESET,  ///< Read ControlPanel::selectedPreset().
    ID_FOCUS_RULE,
    ID_SHOW_CONTROLS_HELP,
};

/// Sends `id` as a wxEVT_MENU command from `source`. Like every command event, it propagates up
/// to MainFrame.
void emitCommand(wxWindow& source, CommandId id);

}  // namespace wxLife::ui

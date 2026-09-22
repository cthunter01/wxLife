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
    RunPauseID = wxID_HIGHEST + 1,
    StepID,
    ClearID,
    RandomizeID,
    ResetAntsID,  ///< Read ControlPanel::antCount().
    FasterID,
    SlowerID,
    ToggleMaxSpeedID,
    SpeedChangedID,  ///< Read ControlPanel::speed().
    AutomatonLifeID,
    AutomatonAntID,
    AutomatonChangedID,  ///< Read ControlPanel::selectedAutomaton().
    EngineBandedID,
    EngineReferenceID,
    ZoomInID,
    ZoomOutID,
    ZoomFitID,
    CenterViewID,
    CellSizeChangedID,  ///< Read ControlPanel::cellSize().
    ToggleGridID,
    WorldSizeID,
    ToggleWrapID,
    ApplyRuleID,   ///< Read ControlPanel::ruleText().
    RulePresetID,  ///< Read ControlPanel::selectedPreset().
    FocusRuleID,
    ShowControlsHelpID,
};

/// Sends `id` as a wxEVT_MENU command from `source`. Like every command event, it propagates up
/// to MainFrame.
void emitCommand(wxWindow& source, CommandId id);

}  // namespace wxLife::ui

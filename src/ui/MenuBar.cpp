#include "wxLife/ui/MenuBar.h"

#include "wxLife/ui/CommandIds.h"
#include "wxLife/ui/WxConvert.h"

namespace wxLife::ui
{

// Accelerators always use Ctrl or a function key, so plain keys keep working in the rule text box.
// Single-key shortcuts live in WorldCanvas::onKeyDown().
wxMenuBar* buildMenuBar()
{
    auto* file = new wxMenu;
    file->Append(wxID_EXIT, "&Quit\tCtrl+Q");

    auto* automaton = new wxMenu;
    automaton->AppendRadioItem(AutomatonLifeID, "&Life");
    automaton->AppendRadioItem(AutomatonAntID, "Langton's &Ant");

    auto* engine = new wxMenu;
    engine->AppendRadioItem(EngineBandedID, "&Banded");
    engine->AppendRadioItem(EngineReferenceID, "&Reference");

    auto* simulation = new wxMenu;
    // MainFrame::syncControls() switches this label to "Pause" while the simulation runs.
    simulation->Append(RunPauseID, "&Run\tF5");
    simulation->Append(StepID, "&Step\tF6");
    simulation->AppendSeparator();
    simulation->Append(FasterID, "&Faster\tCtrl+]");
    simulation->Append(SlowerID, "S&lower\tCtrl+[");
    simulation->AppendCheckItem(ToggleMaxSpeedID, "&Max Speed\tCtrl+M");
    simulation->AppendSeparator();
    // The automaton decides whether the engine below it matters at all, so it comes first.
    simulation->AppendSubMenu(automaton, "&Automaton");
    simulation->AppendSubMenu(engine, "&Engine");

    auto* edit = new wxMenu;
    edit->Append(ClearID, "&Clear\tCtrl+Delete");
    edit->Append(RandomizeID, "&Randomize\tCtrl+R");
    // No accelerator: every free Ctrl key is a GTK binding.
    edit->Append(ResetAntsID, "Reset &Ants");
    edit->AppendSeparator();
    edit->Append(FocusRuleID, toWx("Edit R&ule…\tCtrl+L"));

    auto* world = new wxMenu;
    world->Append(WorldSizeID, toWx("&Size…\tCtrl+N"));
    world->AppendCheckItem(ToggleWrapID, "&Wrap Edges\tCtrl+T");

    auto* view = new wxMenu;
    view->Append(ZoomInID, "Zoom &In\tCtrl+=");
    view->Append(ZoomOutID, "Zoom &Out\tCtrl+-");
    view->Append(ZoomFitID, "&Fit World\tCtrl+0");
    view->Append(CenterViewID, "&Center World\tCtrl+Home");
    view->AppendSeparator();
    view->AppendCheckItem(ToggleGridID, "&Grid Lines\tCtrl+G");

    auto* help = new wxMenu;
    help->Append(ShowControlsHelpID, toWx("&Keyboard and Mouse…\tF1"));
    help->Append(wxID_ABOUT, "&About wxLife");

    auto* bar = new wxMenuBar;
    bar->Append(file, "&File");
    bar->Append(simulation, "&Simulation");
    bar->Append(edit, "&Edit");
    bar->Append(world, "&World");
    bar->Append(view, "&View");
    bar->Append(help, "&Help");
    return bar;
}

}  // namespace wxLife::ui

// Smoke test of the whole wx user interface. Each test creates a real MainFrame for a World it
// owns, shows it, and drives it the way a user would:
// - commands through emitCommand(), exactly what menu items, accelerators and canvas keys send;
// - synthetic wx events for panel controls, canvas keys, the mouse, the wheel and the scrollbars;
// - text typed into number boxes through GTK, which filters it as it filters real typing.
// The checks read the World and what the window shows: menus, panel controls, the status bar and
// the canvas scrollbars. Modal dialogs are answered by a wxModalDialogHook, so none is ever shown.
//
// Needs a display (X11, Wayland or GTK's Broadway). Without one every test is skipped; a display
// that is configured but cannot be opened fails every test. Paints are checked only while the
// display draws windows, which a locked or switched-off screen may not do.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>
#include <ostream>
#include <print>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <wx/app.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dcclient.h>
#include <wx/dialog.h>
#include <wx/evtloop.h>
#include <wx/frame.h>
#include <wx/init.h>
#include <wx/log.h>
#include <wx/menu.h>
#include <wx/modalhook.h>
#include <wx/slider.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/statusbr.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <wx/utils.h>
#include <wx/weakref.h>

#include "wxLife/core/Ant.h"
#include "wxLife/core/Format.h"
#include "wxLife/core/Rule.h"
#include "wxLife/core/Speed.h"
#include "wxLife/core/Stepper.h"
#include "wxLife/core/Types.h"
#include "wxLife/core/World.h"
#include "wxLife/core/WorldLimits.h"
#include "wxLife/render/Viewport.h"
#include "wxLife/ui/CommandIds.h"
#include "wxLife/ui/ControlPanel.h"
#include "wxLife/ui/Defaults.h"
#include "wxLife/ui/MainFrame.h"
#include "wxLife/ui/Theme.h"
#include "wxLife/ui/WorldCanvas.h"
#include "wxLife/ui/WxConvert.h"

// GTK's setter for the text of an entry, such as a number box. Declared here because the tests do
// not see GTK's headers (wx/defs.h declares GtkWidget).
// NOLINTNEXTLINE(readability-identifier-naming): GTK names it
extern "C" void gtk_entry_set_text(GtkWidget* entry, const char* text);

namespace wxLife::core
{
// Lets GoogleTest print cell positions in failure messages. It is found by argument-dependent
// lookup, which does not search unnamed namespaces, so it is static instead.
static void PrintTo(CellPos cell, std::ostream* out)  // NOLINT(misc-use-anonymous-namespace)
{
    *out << '(' << cell.x << ", " << cell.y << ')';
}
}  // namespace wxLife::core

namespace wxLife::ui
{
namespace
{

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

// -------------------------------------------------------------------------------------------------
// The wx session: started once per process, without a main loop.
// -------------------------------------------------------------------------------------------------

// wx assertion failures and logged errors. Each one fails the test that caused it.
std::vector<std::string>& problems()
{
    static std::vector<std::string> s_list;
    return s_list;
}

void recordAssertion(const wxString& file, int line, const wxString& function,
                     const wxString& condition, const wxString& message)
{
    problems().push_back("wx assertion '" + toUtf8(condition) + "' failed in " + toUtf8(function) +
                         " (" + toUtf8(file) + ":" + std::to_string(line) +
                         "): " + toUtf8(message));
}

// Keeps errors and warnings for the test; wx's default GUI log target would show them in a dialog.
class RecordingLog final : public wxLog
{
protected:
    void DoLogTextAtLevel(wxLogLevel level, const wxString& message) override
    {
        if (level <= wxLOG_Warning)
        {
            problems().push_back("wx log: " + toUtf8(message));
        }
    }
};

// Runs the wx event loop, as the application's main loop would (timers, paints, deleting closed
// frames), until `done()` holds or `timeout` has passed. Returns done().
bool runUntil(const std::function<bool()>& done, std::chrono::milliseconds timeout = 5s)
{
    const Clock::time_point deadline = Clock::now() + timeout;
    wxGUIEventLoop          loop;
    wxTimer                 poll;
    poll.Bind(wxEVT_TIMER, [&](wxTimerEvent&) {
        if (done() || Clock::now() >= deadline)
        {
            poll.Stop();
            loop.Exit();
        }
    });
    poll.Start(5);
    loop.Run();
    return done();
}

void runFor(std::chrono::milliseconds duration)
{
    runUntil([] { return false; }, duration);
}

// Whether the display draws windows at the moment. A locked or switched-off screen may draw
// nothing, or only a window's first frame; GTK then repaints no window, whatever the application
// does. Checked with a plain window that has to be painted twice.
bool displayDrawsWindows()
{
    auto* probe =
        new wxFrame(nullptr, wxID_ANY, "wxLife_tests probe", wxDefaultPosition, wxSize(96, 96));
    const wxWeakRef<wxFrame> alive(probe);
    int                      paints = 0;
    probe->Bind(wxEVT_PAINT, [&](wxPaintEvent&) {
        const wxPaintDC dc(probe);
        ++paints;
    });
    probe->ShowWithoutActivating();
    bool draws = runUntil([&] { return paints > 0; }, 2s);
    if (draws)
    {
        const int firstFrame = paints;
        probe->Refresh();
        draws = runUntil([&] { return paints > firstFrame; }, 2s);
    }
    probe->Destroy();
    runUntil([&] { return !alive; });  // no more paints for the local counter
    return draws;
}

// Whether a display is configured: X11, Wayland, or GTK's Broadway backend.
bool displayConfigured()
{
    // getenv() is safe here: nothing sets the environment while the tests run.
    const auto isSet = [](const char* variable) {
        const char* value = std::getenv(variable);  // NOLINT(concurrency-mt-unsafe)
        return value != nullptr && *value != '\0';
    };
    const char* backend = std::getenv("GDK_BACKEND");  // NOLINT(concurrency-mt-unsafe)
    return isSet("DISPLAY") || isSet("WAYLAND_DISPLAY") || isSet("BROADWAY_DISPLAY") ||
           (backend != nullptr && std::string_view(backend).contains("broadway"));
}

// Initialises wx as wxEntry() does, but runs no main loop: the tests run the event loop
// themselves, for as long as each step needs (see runUntil()). The unit tests share this program,
// so wx starts with the GUI test suite (GuiSmokeTest::SetUpTestSuite()), and a process that runs
// no GUI test never opens the display.
class WxSession final
{
public:
    static bool active() { return s_active; }
    /// A display is configured, but wx did not start.
    static bool startFailed() { return s_startFailed; }
    static bool drawsWindows() { return s_drawsWindows; }
    static bool recheckDrawing() { return s_drawsWindows = displayDrawsWindows(); }

    /// Starts wx on the first call; later calls do nothing, because GTK cannot be started twice.
    static void start()
    {
        if (std::exchange(s_started, true) || !displayConfigured())
        {
            return;  // without a display every test skips itself
        }
        // As LifeApp::Initialize() does.
        if (!wxGetEnv("GTK_OVERLAY_SCROLLING", nullptr))
        {
            wxSetEnv("GTK_OVERLAY_SCROLLING", "0");
        }
        wxApp::SetInstance(new wxApp);  // deleted by wxEntryCleanup()
        std::string          name = "wxLife_tests";
        std::array<char*, 2> argv{name.data(), nullptr};
        int                  argc = 1;
        // No ASSERT here: the tests report the failure instead (GuiSmokeTest::SetUp()).
        if (!wxEntryStart(argc, argv.data()))
        {
            s_startFailed = true;
            return;
        }
        s_active = true;
        if (!wxTheApp->CallOnInit())
        {
            s_startFailed = true;
            return;
        }
        wxSetAssertHandler(recordAssertion);
        // wx owns the active log target; the analyzer cannot see that through a system header.
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
        delete wxLog::SetActiveTarget(new RecordingLog);
        if (!recheckDrawing())
        {
            std::println(stderr,
                         "Note: the display draws no windows (screen locked or off?), so paints "
                         "are not checked.");
        }
    }

    /// Destroys any window still left, then the app.
    static void stop()
    {
        if (!s_active)
        {
            return;
        }
        wxTheApp->OnExit();
        wxEntryCleanup();
        s_active = false;
    }

private:
    static inline bool s_started      = false;
    static inline bool s_active       = false;
    static inline bool s_startFailed  = false;
    static inline bool s_drawsWindows = false;
};

// -------------------------------------------------------------------------------------------------
// Finding controls
// -------------------------------------------------------------------------------------------------

// Every descendant of `parent` that is a T, depth first in creation order.
template <typename T>
std::vector<T*> all(wxWindow& parent)
{
    std::vector<T*> found;
    for (wxWindow* child : parent.GetChildren())
    {
        if (child == nullptr)
        {
            continue;
        }
        if (auto* match = dynamic_cast<T*>(child))
        {
            found.push_back(match);
        }
        std::ranges::copy(all<T>(*child), std::back_inserter(found));
    }
    return found;
}

template <typename T>
T& first(wxWindow& parent)
{
    const std::vector<T*> found = all<T>(parent);
    if (found.empty())
    {
        throw std::runtime_error("no such control");
    }
    return *found.front();
}

// The control below `parent` with this label: a button, a check box or a group's static box.
template <typename T>
T& labelled(wxWindow& parent, std::string_view label)
{
    auto* control = dynamic_cast<T*>(wxWindow::FindWindowByLabel(toWx(label), &parent));
    if (control == nullptr)
    {
        throw std::runtime_error("no control labelled '" + std::string(label) + "'");
    }
    return *control;
}

// -------------------------------------------------------------------------------------------------
// User input. As with real input, a control's value changes first, then the control sends its own
// event.
// -------------------------------------------------------------------------------------------------

void deliver(wxWindow& target, wxEvent& event)
{
    event.SetEventObject(&target);
    target.ProcessWindowEvent(event);
}

void click(wxButton& button)
{
    wxCommandEvent event(wxEVT_BUTTON, button.GetId());
    deliver(button, event);
}

void toggle(wxCheckBox& box)
{
    box.SetValue(!box.GetValue());
    wxCommandEvent event(wxEVT_CHECKBOX, box.GetId());
    event.SetInt(box.GetValue() ? 1 : 0);
    deliver(box, event);
}

void choose(wxChoice& choice, int index)
{
    choice.SetSelection(index);
    wxCommandEvent event(wxEVT_CHOICE, choice.GetId());
    event.SetInt(index);
    deliver(choice, event);
}

void type(wxSpinCtrl& spin, int value)
{
    spin.SetValue(value);
    wxSpinEvent event(wxEVT_SPINCTRL, spin.GetId());
    event.SetPosition(value);
    deliver(spin, event);
}

void drag(wxSlider& slider, int position)
{
    slider.SetValue(position);
    wxCommandEvent event(wxEVT_SLIDER, slider.GetId());
    event.SetInt(position);
    deliver(slider, event);
}

// Types `utf8` into the box, replacing its text, and presses Enter.
void typeAndEnter(wxTextCtrl& text, std::string_view utf8)
{
    text.ChangeValue(toWx(utf8));
    wxCommandEvent event(wxEVT_TEXT_ENTER, text.GetId());
    event.SetString(text.GetValue());
    deliver(text, event);
}

// Types `text` into a number box, replacing its contents. GTK filters it as it filters typing, and
// wx sends wxEVT_TEXT.
void typeText(wxSpinCtrl& box, const std::string& text)
{
    gtk_entry_set_text(box.GetHandle(), text.c_str());
}

// A key event of `type` (key down, char or char hook). `modifiers` combines wxMOD_SHIFT,
// wxMOD_CONTROL and wxMOD_ALT; wx reports AltGr as wxMOD_ALTGR, which is Ctrl+Alt.
void sendKey(wxWindow& target, wxEventType type, int keyCode, int modifiers)
{
    wxKeyEvent event(type);
    event.m_keyCode = keyCode;
    event.m_uniChar = static_cast<wxChar>(keyCode < 0x100 ? keyCode : 0);  // as wxGTK sets it
    event.SetShiftDown((modifiers & wxMOD_SHIFT) != 0);
    event.SetControlDown((modifiers & wxMOD_CONTROL) != 0);
    event.SetAltDown((modifiers & wxMOD_ALT) != 0);
    deliver(target, event);
}

void pressKey(wxWindow& target, int keyCode, int modifiers = wxMOD_NONE)
{
    sendKey(target, wxEVT_KEY_DOWN, keyCode, modifiers);
}

// The char event wx sends after a key press that no handler took.
void typeChar(wxWindow& target, char character, int modifiers = wxMOD_NONE)
{
    sendKey(target, wxEVT_CHAR, character, modifiers);
}

// A mouse event at `at`, in logical pixels like the ones wx reports.
void mouse(wxWindow& target, wxEventType type, wxPoint at, int modifiers = wxMOD_NONE)
{
    wxMouseEvent event(type);
    event.SetPosition(at);
    event.SetShiftDown((modifiers & wxMOD_SHIFT) != 0);
    event.SetControlDown((modifiers & wxMOD_CONTROL) != 0);
    deliver(target, event);
}

// Turns the vertical wheel; positive `notches` turn it up (away from the user).
void turnWheel(wxWindow& target, wxPoint at, int notches, int modifiers = wxMOD_NONE)
{
    wxMouseEvent event(wxEVT_MOUSEWHEEL);
    event.SetPosition(at);
    event.SetControlDown((modifiers & wxMOD_CONTROL) != 0);
    event.m_wheelAxis     = wxMOUSE_WHEEL_VERTICAL;
    event.m_wheelDelta    = 120;  // what GTK reports for one notch
    event.m_wheelRotation = notches * event.m_wheelDelta;
    deliver(target, event);
}

void scroll(wxWindow& target, wxEventType type, int orientation, int position = 0)
{
    wxScrollWinEvent event(type, position, orientation);
    deliver(target, event);
}

// Answers modal dialogs instead of showing them. wx calls Enter() before it shows a modal dialog,
// and ShowModal() returns any answer other than wxID_NONE at once.
class DialogAnswers final : public wxModalDialogHook
{
public:
    /// What to type into "World Size".
    struct SizeEntry
    {
        std::string width;
        std::string height;
        bool        keepPattern = true;
    };

    DialogAnswers() { Register(); }  // the base destructor unregisters

    std::optional<SizeEntry> worldSize;  ///< nullopt cancels "World Size".
    std::vector<std::string> titles;     ///< Every dialog asked, in order.
    std::vector<std::string>
        sizeTexts;  ///< The texts the last "World Size" showed after the typing.

protected:
    int Enter(wxDialog* dialog) override
    {
        titles.push_back(toUtf8(dialog->GetTitle()));
        if (titles.back() != "World Size")
        {
            return wxID_OK;  // message boxes
        }
        if (!worldSize)
        {
            return wxID_CANCEL;
        }
        // Like a user: type both sides (width first), set the check box, and press Enter. wxGTK
        // then presses OK even while it is disabled, and wx accepts the dialog only if Validate()
        // holds.
        const std::vector<wxSpinCtrl*> sides = all<wxSpinCtrl>(*dialog);
        typeText(*sides.at(0), worldSize->width);
        typeText(*sides.at(1), worldSize->height);
        first<wxCheckBox>(*dialog).SetValue(worldSize->keepPattern);
        sizeTexts.clear();
        for (const wxStaticText* text : all<wxStaticText>(*dialog))
        {
            sizeTexts.push_back(toUtf8(text->GetLabelText()));
        }
        const bool accepted = dialog->Validate();
        EXPECT_EQ(dialog->FindWindow(wxID_OK)->IsEnabled(), accepted);
        return accepted ? wxID_OK : wxID_CANCEL;
    }
};

// -------------------------------------------------------------------------------------------------
// The fixture: one shown MainFrame per test, closed and destroyed afterwards.
// -------------------------------------------------------------------------------------------------

class GuiSmokeTest : public testing::Test
{
protected:
    enum class StatusField : std::uint8_t
    {
        State,
        Generation,
        Population,
        Speed,
        World,
        View
    };

    static void SetUpTestSuite() { WxSession::start(); }
    static void TearDownTestSuite() { WxSession::stop(); }

    void SetUp() override
    {
        if (WxSession::startFailed())
        {
            FAIL() << "A display is configured, but wx could not start on it.";
        }
        if (!WxSession::active())
        {
            GTEST_SKIP()
                << "No display: DISPLAY, WAYLAND_DISPLAY and BROADWAY_DISPLAY are all unset.";
        }

        m_frame  = new MainFrame(m_world);  // wx deletes it after it is closed
        m_canvas = &first<WorldCanvas>(*m_frame);
        m_panel  = &first<ControlPanel>(*m_frame);
        // Handlers bound later run first, so this counts every paint and Skip() lets the canvas
        // draw it.
        m_canvas->Bind(wxEVT_PAINT, [this](wxPaintEvent& event) {
            ++m_paints;
            event.Skip();
        });
        // Without activation, keys pressed by the person at the desktop go where they did.
        m_frame->ShowWithoutActivating();
        paintedSince(0);
    }

    void TearDown() override
    {
        if (!WxSession::active())
        {
            return;
        }
        if (m_frame.get() != nullptr)
        {
            m_frame->Close();
            EXPECT_TRUE(runUntil([this] { return !m_frame; }))
                << "The closed frame was not destroyed.";
            delete m_frame.get();  // only left if the check failed; it must not outlive m_world
        }
        EXPECT_EQ(wxWindow::GetCapture(), nullptr);
        for (const std::string& problem : std::exchange(problems(), {}))
        {
            ADD_FAILURE() << problem;
        }
    }

    // What a menu item, its accelerator or a canvas key sends.
    void command(CommandId id) { emitCommand(*m_frame, id); }

    [[nodiscard]] wxMenuItem& menuItem(CommandId id) const
    {
        return *m_frame->GetMenuBar()->FindItem(id);
    }
    [[nodiscard]] std::string menuLabel(CommandId id) const
    {
        return toUtf8(menuItem(id).GetItemLabelText());
    }

    [[nodiscard]] std::string status(StatusField field) const
    {
        return toUtf8(m_frame->GetStatusBar()->GetStatusText(std::to_underlying(field)));
    }

    [[nodiscard]] wxStaticBox& group(std::string_view label) const
    {
        return labelled<wxStaticBox>(*m_panel, label);
    }

    [[nodiscard]] wxPoint canvasCentre() const
    {
        const wxSize size = m_canvas->GetClientSize();
        return {size.x / 2, size.y / 2};
    }

    // The cell the status bar shows under the pointer (nullopt for "–").
    [[nodiscard]] std::optional<core::CellPos> hoveredCell() const
    {
        static const std::regex kCell(R"(^\((-?[0-9]+), (-?[0-9]+)\))");
        const std::string       text = status(StatusField::View);
        std::smatch             match;
        if (!std::regex_search(text, match, kCell))
        {
            return std::nullopt;
        }
        return core::CellPos{.x = std::stoi(match[1].str()), .y = std::stoi(match[2].str())};
    }

    // Moves the pointer to `at` and returns hoveredCell(). During a drag this paints or pans, like
    // a real mouse move.
    std::optional<core::CellPos> pointAt(wxPoint at)
    {
        mouse(*m_canvas, wxEVT_MOTION, at);
        return hoveredCell();
    }

    // Waits until the canvas has been painted more than `before` times. Returns false, without
    // failing, while the display draws no windows at all. Most callers only wait, so the result
    // may be ignored.
    bool paintedSince(int before) const  // NOLINT(modernize-use-nodiscard)
    {
        if (!WxSession::drawsWindows())
        {
            return false;
        }
        if (runUntil([&] { return m_paints > before; }))
        {
            return true;
        }
        // The screen may have been locked or switched off in the meantime.
        EXPECT_FALSE(WxSession::recheckDrawing())
            << "The canvas was not painted, although the display draws other windows.";
        return false;
    }

    // Asks for a repaint and waits for it, as paintedSince(). Update() paints at once where the
    // backend allows it (X11).
    bool repaint() const  // NOLINT(modernize-use-nodiscard)
    {
        const int before = m_paints;
        m_canvas->Refresh(false);
        m_canvas->Update();
        return paintedSince(before);
    }

    // The cell size that shows the whole world on the current canvas
    // (render::Viewport::fitWorld()).
    [[nodiscard]] int fittedCellSize() const
    {
        const wxSize       logical = m_canvas->GetClientSize();
        const double       scale   = m_canvas->GetContentScaleFactor();
        const core::Extent world   = m_world.extent();
        const long         fit     = std::min(std::lround(logical.x * scale) / world.width,
                                              std::lround(logical.y * scale) / world.height);
        return static_cast<int>(std::clamp<long>(fit, render::kMinCellSize, render::kMaxCellSize));
    }

    [[nodiscard]] core::CellCount countedPopulation() const { return m_world.cells().countAlive(); }

    [[nodiscard]] static std::string countText(std::int64_t n)
    {
        return core::formatCount(static_cast<std::uint64_t>(n));
    }

    core::World          m_world{defaults::kWorldExtent, core::Rule{}, defaults::kTopology};
    wxWeakRef<MainFrame> m_frame;  ///< Becomes null once wx has deleted the frame.
    WorldCanvas*         m_canvas = nullptr;
    ControlPanel*        m_panel  = nullptr;
    int                  m_paints = 0;
};

TEST_F(GuiSmokeTest, OpensPausedWithARandomWorldFitted)
{
    // MainFrame's constructor fills 25% of the world at random (the panel's density).
    EXPECT_EQ(m_world.generation(), 0U);
    EXPECT_NEAR(static_cast<double>(m_world.population()) /
                    static_cast<double>(m_world.extent().cellCount()),
                0.25, 0.01);
    EXPECT_EQ(m_world.population(), countedPopulation());

    EXPECT_EQ(status(StatusField::State), "Paused");
    EXPECT_EQ(status(StatusField::Generation), "Gen 0");
    EXPECT_EQ(status(StatusField::Population), "Pop " + countText(m_world.population()));
    EXPECT_EQ(status(StatusField::Speed), "30 gen/s");
    EXPECT_EQ(status(StatusField::World), "512 × 512 · torus · B3/S23 · Banded");

    // The world is fitted to the canvas. The canvas notices a late change of its size or display
    // scale when it paints, so this is checked after a paint.
    if (repaint())
    {
        EXPECT_EQ(m_canvas->cellSize(), fittedCellSize());
    }
    EXPECT_EQ(m_panel->cellSize(), m_canvas->cellSize());
    EXPECT_TRUE(m_canvas->showGrid());
    EXPECT_EQ(m_panel->speed(), defaults::kSpeed);
    EXPECT_EQ(m_panel->ruleText(), "B3/S23");
    EXPECT_EQ(m_panel->selectedPreset(), 0U);  // Conway's Life

    EXPECT_EQ(menuLabel(RunPauseID), "Run");
    EXPECT_FALSE(menuItem(ToggleMaxSpeedID).IsChecked());
    EXPECT_TRUE(menuItem(EngineBandedID).IsChecked());
    EXPECT_TRUE(menuItem(EngineReferenceID).IsEnabled());
    EXPECT_TRUE(menuItem(ToggleWrapID).IsChecked());
    EXPECT_TRUE(menuItem(ToggleGridID).IsChecked());

    // Life runs until the user asks for the other automaton, and it has no ants.
    EXPECT_EQ(m_world.automaton(), core::Automaton::Life);
    EXPECT_TRUE(m_world.ants().empty());
    EXPECT_TRUE(menuItem(AutomatonLifeID).IsChecked());
    EXPECT_FALSE(menuItem(ResetAntsID).IsEnabled());
}

TEST_F(GuiSmokeTest, RunsPausesAndSteps)
{
    // Run for about half a second at the default 30 generations per second.
    const Clock::time_point started      = Clock::now();
    const int               paintsBefore = m_paints;
    command(RunPauseID);
    EXPECT_EQ(status(StatusField::State), "Running");
    EXPECT_EQ(status(StatusField::Speed),
              "30 gen/s");  // only the target until a rate has been measured
    EXPECT_EQ(menuLabel(RunPauseID), "Pause");
    EXPECT_NO_THROW(labelled<wxButton>(*m_panel, "Pause"));
    runFor(500ms);
    EXPECT_TRUE(runUntil([this] { return m_world.generation() > 0; }));
    const std::regex measured(R"(30 gen/s \([0-9]+\.[0-9]\))");  // target and measured rate
    EXPECT_TRUE(runUntil([&] { return std::regex_match(status(StatusField::Speed), measured); }))
        << status(StatusField::Speed);
    command(RunPauseID);
    const double seconds = std::chrono::duration<double>(Clock::now() - started).count();

    // The pacer never runs ahead of the target rate, and the ticks repaint the canvas (several
    // paints, not just one that was still pending).
    const std::uint64_t generation = m_world.generation();
    EXPECT_LE(static_cast<double>(generation), (seconds * defaults::kSpeed.gensPerSecond) + 1);
    paintedSince(paintsBefore + 2);
    EXPECT_EQ(m_world.population(), countedPopulation());
    EXPECT_EQ(status(StatusField::State), "Paused");
    EXPECT_EQ(status(StatusField::Generation), "Gen " + core::formatCount(generation));
    EXPECT_EQ(status(StatusField::Population), "Pop " + countText(m_world.population()));
    EXPECT_EQ(menuLabel(RunPauseID), "Run");

    // Paused means paused.
    runFor(200ms);
    EXPECT_EQ(m_world.generation(), generation);

    // One generation at a time: the menu command, the panel button, and N on the canvas.
    command(StepID);
    click(labelled<wxButton>(*m_panel, "Step"));
    pressKey(*m_canvas, 'N');
    EXPECT_EQ(m_world.generation(), generation + 3);
    EXPECT_EQ(status(StatusField::Generation), "Gen " + core::formatCount(generation + 3));
    EXPECT_EQ(m_world.population(), countedPopulation());

    // Space and the panel button toggle too. Step while running pauses first.
    pressKey(*m_canvas, WXK_SPACE);
    EXPECT_EQ(status(StatusField::State), "Running");
    command(StepID);
    EXPECT_EQ(status(StatusField::State), "Paused");
    EXPECT_EQ(m_world.generation(), generation + 4);
    click(labelled<wxButton>(*m_panel, "Run"));
    EXPECT_EQ(status(StatusField::State), "Running");
    click(labelled<wxButton>(*m_panel, "Pause"));
    EXPECT_EQ(status(StatusField::State), "Paused");
}

TEST_F(GuiSmokeTest, ClearsRandomizesAndDrawsWithTheMouse)
{
    command(ClearID);
    EXPECT_EQ(m_world.population(), 0);
    EXPECT_EQ(status(StatusField::Population), "Pop 0");

    // Randomize reads the density spin control, which sends nothing itself.
    auto& density = first<wxSpinCtrl>(group("Simulation"));
    density.SetValue(100);
    command(RandomizeID);
    EXPECT_EQ(m_world.population(), m_world.extent().cellCount());
    density.SetValue(10);
    click(labelled<wxButton>(*m_panel, "Randomize"));
    EXPECT_NEAR(static_cast<double>(m_world.population()) /
                    static_cast<double>(m_world.extent().cellCount()),
                0.10, 0.01);
    EXPECT_EQ(m_world.population(), countedPopulation());
    EXPECT_EQ(m_world.generation(), 0U);
    click(labelled<wxButton>(*m_panel, "Clear"));
    EXPECT_EQ(m_world.population(), 0);

    // At 8 px a short drag crosses several cells. A left drag from a dead cell draws a line.
    type(first<wxSpinCtrl>(group("View")), 8);
    ASSERT_EQ(m_canvas->cellSize(), 8);
    const wxPoint                      start     = canvasCentre();
    const wxPoint                      end       = start + wxPoint(40, 16);
    const std::optional<core::CellPos> startCell = pointAt(start);
    ASSERT_TRUE(startCell);
    mouse(*m_canvas, wxEVT_LEFT_DOWN, start);
    EXPECT_EQ(m_world.at(startCell.value()), core::kAlive);
    EXPECT_EQ(wxWindow::GetCapture(), m_canvas);
    const std::optional<core::CellPos> endCell = pointAt(end);
    ASSERT_TRUE(endCell);
    mouse(*m_canvas, wxEVT_LEFT_UP, end);
    EXPECT_EQ(wxWindow::GetCapture(), nullptr);

    // The line has no gaps: one cell per step along its longer axis.
    const core::CellCount length = std::max(std::abs(endCell.value().x - startCell.value().x),
                                            std::abs(endCell.value().y - startCell.value().y)) +
                                   1;
    EXPECT_GT(length, 2);
    EXPECT_EQ(m_world.population(), length);
    EXPECT_EQ(m_world.at(endCell.value()), core::kAlive);
    EXPECT_EQ(status(StatusField::Population), "Pop " + countText(length));

    // A left press on a live cell erases it; the right button always erases.
    mouse(*m_canvas, wxEVT_LEFT_DOWN, end);
    mouse(*m_canvas, wxEVT_LEFT_UP, end);
    EXPECT_EQ(m_world.at(endCell.value()), core::kDead);
    mouse(*m_canvas, wxEVT_RIGHT_DOWN, start);
    pointAt(end);
    mouse(*m_canvas, wxEVT_RIGHT_UP, end);
    EXPECT_EQ(m_world.population(), 0);

    // Esc ends a stroke and releases the mouse.
    mouse(*m_canvas, wxEVT_LEFT_DOWN, start);
    pressKey(*m_canvas, WXK_ESCAPE);
    EXPECT_EQ(wxWindow::GetCapture(), nullptr);
    pointAt(end);
    EXPECT_EQ(m_world.population(), 1);

    // Shift+left drag pans: the cell under the pointer moves along with it, and nothing is drawn.
    mouse(*m_canvas, wxEVT_LEFT_DOWN, start, wxMOD_SHIFT);
    pointAt(end);
    mouse(*m_canvas, wxEVT_LEFT_UP, end);
    EXPECT_EQ(pointAt(end), startCell);
    EXPECT_EQ(m_world.population(), 1);

    // Only the button that started a drag ends it: a right click during a left drag changes
    // nothing.
    command(ClearID);
    const std::optional<core::CellPos> from = pointAt(start);
    mouse(*m_canvas, wxEVT_LEFT_DOWN, start);
    mouse(*m_canvas, wxEVT_RIGHT_DOWN, start);
    mouse(*m_canvas, wxEVT_RIGHT_UP, start);
    EXPECT_EQ(wxWindow::GetCapture(), m_canvas);
    const std::optional<core::CellPos> to = pointAt(end);
    mouse(*m_canvas, wxEVT_LEFT_UP, end);
    ASSERT_TRUE(from && to);
    EXPECT_EQ(m_world.population(), std::max(std::abs(to.value().x - from.value().x),
                                             std::abs(to.value().y - from.value().y)) +
                                        1);

    // When the camera moves during a stroke, the stroke goes on from the cell now under the
    // pointer, with no line across the jump.
    command(ClearID);
    const wxPoint                      aside   = canvasCentre() + wxPoint(-100, -60);
    const std::optional<core::CellPos> pressed = pointAt(aside);
    mouse(*m_canvas, wxEVT_LEFT_DOWN, aside);
    pressKey(*m_canvas, 'F');  // fit: the pointer is now over a cell far away
    const std::optional<core::CellPos> jumped = pointAt(aside + wxPoint(1, 0));
    mouse(*m_canvas, wxEVT_LEFT_UP, aside + wxPoint(1, 0));
    ASSERT_TRUE(pressed && jumped);
    EXPECT_GT(std::abs(jumped.value().x - pressed.value().x), 1);
    EXPECT_EQ(m_world.population(), 2);
    EXPECT_EQ(m_world.at(jumped.value()), core::kAlive);
}

TEST_F(GuiSmokeTest, ChangesSpeed)
{
    command(FasterID);
    EXPECT_EQ(m_panel->speed().gensPerSecond, 60);
    EXPECT_EQ(status(StatusField::Speed), "60 gen/s");
    command(SlowerID);
    typeChar(*m_canvas, '[');
    EXPECT_EQ(m_panel->speed().gensPerSecond, 20);
    // The canvas matches these keys by the typed character. On a German layout ']' is AltGr+9,
    // whose key-down event says '9'.
    pressKey(*m_canvas, '9', wxMOD_ALTGR);
    typeChar(*m_canvas, ']', wxMOD_ALTGR);
    EXPECT_EQ(m_panel->speed().gensPerSecond, 30);
    typeChar(*m_canvas, ']', wxMOD_ALT);  // a plain Alt combination is left to the menus
    EXPECT_EQ(m_panel->speed().gensPerSecond, 30);

    // The spin control and the logarithmic slider show the same rate.
    auto& rate   = first<wxSpinCtrl>(group("Speed"));
    auto& slider = first<wxSlider>(group("Speed"));
    type(rate, 1000);
    EXPECT_EQ(slider.GetValue(), core::Speed::kSliderMax);
    EXPECT_EQ(status(StatusField::Speed), "1000 gen/s");
    drag(slider, 100);  // 10^(100 / 100)
    EXPECT_EQ(rate.GetValue(), 10);
    EXPECT_EQ(status(StatusField::Speed), "10 gen/s");

    // Faster from 1000 reaches Max; Slower leaves it and keeps the rate.
    type(rate, 1000);
    command(FasterID);
    EXPECT_TRUE(m_panel->speed().unlimited);
    EXPECT_TRUE(menuItem(ToggleMaxSpeedID).IsChecked());
    EXPECT_FALSE(rate.IsEnabled());
    EXPECT_EQ(status(StatusField::Speed), "Max");
    command(SlowerID);
    EXPECT_EQ(m_panel->speed(), (core::Speed{.gensPerSecond = 1000}));
    EXPECT_TRUE(rate.IsEnabled());

    // The check box switches Max on. The runner then steps several generations per tick.
    type(rate, 30);
    toggle(labelled<wxCheckBox>(*m_panel, "Max speed"));
    EXPECT_TRUE(m_panel->speed().unlimited);
    command(RunPauseID);
    runFor(300ms);
    EXPECT_GT(m_world.generation(), 30U);  // one generation per 16 ms tick would give about 19
    // Once the first half-second window has passed, the status bar shows the measured rate.
    const std::regex measuredMax(R"(Max \([1-9][0-9,]* gen/s\))");
    EXPECT_TRUE(runUntil([&] { return std::regex_match(status(StatusField::Speed), measuredMax); }))
        << status(StatusField::Speed);
    command(RunPauseID);
    EXPECT_EQ(m_world.population(), countedPopulation());

    command(ToggleMaxSpeedID);
    EXPECT_EQ(m_panel->speed(), (core::Speed{.gensPerSecond = 30}));
    EXPECT_FALSE(menuItem(ToggleMaxSpeedID).IsChecked());
    EXPECT_EQ(status(StatusField::Speed), "30 gen/s");

    // The frame takes Ctrl+M before any control sees it; in the rule box wxGTK would turn it into
    // Enter.
    auto& ruleText = first<wxTextCtrl>(group("Rule"));
    sendKey(ruleText, wxEVT_CHAR_HOOK, 'M', wxMOD_CONTROL);
    EXPECT_TRUE(m_panel->speed().unlimited);
    sendKey(ruleText, wxEVT_CHAR_HOOK, 'M', wxMOD_CONTROL | wxMOD_SHIFT);  // only plain Ctrl+M
    EXPECT_TRUE(m_panel->speed().unlimited);
    sendKey(*m_canvas, wxEVT_CHAR_HOOK, 'M', wxMOD_CONTROL);
    EXPECT_FALSE(m_panel->speed().unlimited);
}

TEST_F(GuiSmokeTest, ZoomsScrollsFitsAndCentres)
{
    auto& cellSize   = first<wxSpinCtrl>(group("View"));
    auto& zoomSlider = first<wxSlider>(group("View"));

    // Both ends of the zoom range, painted at each.
    for (std::size_t i = 0; i < render::kZoomSteps.size(); ++i)
    {
        command(ZoomOutID);
    }
    EXPECT_EQ(m_canvas->cellSize(), render::kMinCellSize);
    EXPECT_EQ(cellSize.GetValue(), render::kMinCellSize);
    EXPECT_TRUE(status(StatusField::View).ends_with("· 1 px · grid hidden < 5 px"))
        << status(StatusField::View);
    repaint();
    for (std::size_t i = 0; i < render::kZoomSteps.size(); ++i)
    {
        command(ZoomInID);
    }
    EXPECT_EQ(m_canvas->cellSize(), render::kMaxCellSize);
    EXPECT_EQ(cellSize.GetValue(), render::kMaxCellSize);
    EXPECT_EQ(zoomSlider.GetValue(), static_cast<int>(render::kZoomSteps.size() - 1));
    EXPECT_TRUE(status(StatusField::View).ends_with("· 100 px")) << status(StatusField::View);
    repaint();

    // Centre: the middle cell is under the canvas centre, and both scrollbars sit halfway.
    command(CenterViewID);
    const std::optional<core::CellPos> middle = pointAt(canvasCentre());
    ASSERT_TRUE(middle);
    EXPECT_NEAR(middle.value().x, 256, 1);
    EXPECT_NEAR(middle.value().y, 256, 1);
    for (const int orientation : {wxHORIZONTAL, wxVERTICAL})
    {
        const int range = m_canvas->GetScrollRange(orientation);  // device pixels, like the thumb
        EXPECT_EQ(range, 512 * render::kMaxCellSize);
        EXPECT_EQ(m_canvas->GetScrollPos(orientation),
                  (range - m_canvas->GetScrollThumb(orientation)) / 2);
    }

    // Scrollbar events, then arrow keys, move the view.
    const int end = m_canvas->GetScrollRange(wxVERTICAL) - m_canvas->GetScrollThumb(wxVERTICAL);
    scroll(*m_canvas, wxEVT_SCROLLWIN_BOTTOM, wxVERTICAL);
    scroll(*m_canvas, wxEVT_SCROLLWIN_TOP, wxHORIZONTAL);
    EXPECT_EQ(m_canvas->GetScrollPos(wxVERTICAL), end);
    EXPECT_EQ(m_canvas->GetScrollPos(wxHORIZONTAL), 0);
    const wxPoint bottomLeft{0, m_canvas->GetClientSize().y - 1};
    EXPECT_EQ(pointAt(bottomLeft), (core::CellPos{0, 511}));  // the world's bottom-left cell
    scroll(*m_canvas, wxEVT_SCROLLWIN_THUMBTRACK, wxHORIZONTAL, 1000);
    EXPECT_EQ(m_canvas->GetScrollPos(wxHORIZONTAL), 1000);
    pressKey(*m_canvas, WXK_RIGHT);  // 10% of the canvas width
    EXPECT_EQ(m_canvas->GetScrollPos(wxHORIZONTAL),
              1000 + (m_canvas->GetScrollThumb(wxHORIZONTAL) / 10));
    pressKey(*m_canvas, WXK_PAGEUP);
    EXPECT_LT(m_canvas->GetScrollPos(wxVERTICAL), end);

    // Ctrl+Home centres, handled by the canvas itself: GTK's scrolled window would take it from the
    // menu.
    pressKey(*m_canvas, WXK_HOME, wxMOD_CONTROL);
    for (const int orientation : {wxHORIZONTAL, wxVERTICAL})
    {
        const int centred =
            (m_canvas->GetScrollRange(orientation) - m_canvas->GetScrollThumb(orientation)) / 2;
        EXPECT_EQ(m_canvas->GetScrollPos(orientation), centred);
    }

    // Fit shows the whole world, from the menu, the F key and the panel button alike.
    command(ZoomFitID);
    const int fitted = fittedCellSize();
    EXPECT_EQ(m_canvas->cellSize(), fitted);
    typeChar(*m_canvas, '+');
    const int zoomedIn = m_canvas->cellSize();
    EXPECT_GT(zoomedIn, fitted);
    typeChar(*m_canvas, '=');
    typeChar(*m_canvas, '-');
    EXPECT_EQ(m_canvas->cellSize(), zoomedIn);
    pressKey(*m_canvas, 'F');
    EXPECT_EQ(m_canvas->cellSize(), fitted);
    pressKey(*m_canvas, WXK_NUMPAD_ADD);
    click(labelled<wxButton>(*m_panel, "Fit"));
    EXPECT_EQ(m_canvas->cellSize(), fitted);
    EXPECT_EQ(cellSize.GetValue(), fitted);

    // The spin control takes any size; the slider moves along the zoom steps.
    type(cellSize, 37);
    EXPECT_EQ(m_canvas->cellSize(), 37);
    EXPECT_EQ(zoomSlider.GetValue(), static_cast<int>(render::nearestZoomStep(37)));
    drag(zoomSlider, 3);
    EXPECT_EQ(m_canvas->cellSize(), render::kZoomSteps[3]);
    EXPECT_EQ(cellSize.GetValue(), render::kZoomSteps[3]);

    // Ctrl+wheel zooms at the pointer, which stays over the same cell.
    type(cellSize, 16);
    pressKey(*m_canvas, 'C');
    const wxPoint                      pointer = canvasCentre() + wxPoint(37, -23);
    const std::optional<core::CellPos> cell    = pointAt(pointer);
    ASSERT_TRUE(cell);
    turnWheel(*m_canvas, pointer, 1, wxMOD_CONTROL);
    EXPECT_EQ(m_canvas->cellSize(), 20);
    EXPECT_EQ(pointAt(pointer), cell);
    turnWheel(*m_canvas, pointer, -2, wxMOD_CONTROL);
    EXPECT_EQ(m_canvas->cellSize(), 12);
    EXPECT_EQ(pointAt(pointer), cell);

    // A plain wheel notch down scrolls 3 lines of max(cell size, 16) px.
    const int top = m_canvas->GetScrollPos(wxVERTICAL);
    turnWheel(*m_canvas, pointer, -1);
    EXPECT_EQ(m_canvas->GetScrollPos(wxVERTICAL), top + (3 * 16));

    // Ctrl and a key belong to the menu accelerators, so the canvas ignores it.
    pressKey(*m_canvas, '+', wxMOD_CONTROL);
    typeChar(*m_canvas, '+', wxMOD_CONTROL);
    EXPECT_EQ(m_canvas->cellSize(), 12);

    // When the view moves under a resting pointer, the status bar shows the cell now under it.
    const auto expectHoverFollows = [&](const std::function<void()>& moveView) {
        const std::optional<core::CellPos> before = pointAt(pointer);
        moveView();
        const std::optional<core::CellPos> shown = hoveredCell();
        const std::optional<core::CellPos> under = pointAt(pointer);  // the same spot again
        EXPECT_NE(under, before);
        EXPECT_EQ(shown, under);
    };
    expectHoverFollows([&] { command(ZoomInID); });
    expectHoverFollows([&] { pressKey(*m_canvas, WXK_DOWN); });
    expectHoverFollows([&] { scroll(*m_canvas, wxEVT_SCROLLWIN_PAGEUP, wxVERTICAL); });
}

TEST_F(GuiSmokeTest, TogglesGridLinesAndWrapping)
{
    // Below 5 px the lines would hide the cells, and the status bar says why none are drawn.
    auto& gridLines = labelled<wxCheckBox>(*m_panel, "Grid lines");
    type(first<wxSpinCtrl>(group("View")), 4);
    EXPECT_TRUE(status(StatusField::View).ends_with("· 4 px · grid hidden < 5 px"))
        << status(StatusField::View);
    command(ToggleGridID);
    EXPECT_FALSE(m_canvas->showGrid());
    EXPECT_FALSE(gridLines.GetValue());
    EXPECT_FALSE(menuItem(ToggleGridID).IsChecked());
    EXPECT_TRUE(status(StatusField::View).ends_with("· 4 px")) << status(StatusField::View);
    pressKey(*m_canvas, 'G');
    EXPECT_TRUE(m_canvas->showGrid());
    EXPECT_TRUE(menuItem(ToggleGridID).IsChecked());

    // At 20 px, paint once with grid lines and once without.
    type(first<wxSpinCtrl>(group("View")), 20);
    repaint();
    toggle(gridLines);
    EXPECT_FALSE(m_canvas->showGrid());
    repaint();

    // A theme-change event keeps the grid setting and leaves the colours of the current theme. (The
    // desktop theme itself does not change here, so this cannot tell whether the colours are picked
    // again.)
    wxSysColourChangedEvent themeChanged;
    deliver(*m_frame, themeChanged);
    EXPECT_FALSE(m_canvas->showGrid());
    EXPECT_EQ(m_canvas->style().outside, themeStyle().outside);
    repaint();

    // Wrap edges switches the world's topology.
    auto& wrap = labelled<wxCheckBox>(*m_panel, "Wrap edges");
    command(ToggleWrapID);
    EXPECT_EQ(m_world.topology(), core::Topology::Bounded);
    EXPECT_FALSE(wrap.GetValue());
    EXPECT_FALSE(menuItem(ToggleWrapID).IsChecked());
    EXPECT_EQ(status(StatusField::World), "512 × 512 · bounded · B3/S23 · Banded");
    pressKey(*m_canvas, 'W');
    EXPECT_EQ(m_world.topology(), core::Topology::Torus);
    EXPECT_TRUE(wrap.GetValue());
    toggle(wrap);
    EXPECT_EQ(m_world.topology(), core::Topology::Bounded);
    EXPECT_FALSE(menuItem(ToggleWrapID).IsChecked());
}

TEST_F(GuiSmokeTest, AppliesPresetAndTypedRules)
{
    auto& presets  = first<wxChoice>(group("Rule"));
    auto& ruleText = first<wxTextCtrl>(group("Rule"));

    choose(presets, 1);
    EXPECT_EQ(m_world.rule(), core::kRulePresets[1].rule);  // HighLife
    EXPECT_EQ(m_panel->ruleText(), "B36/S23");
    EXPECT_EQ(status(StatusField::World), "512 × 512 · torus · B36/S23 · Banded");

    // Typed text, confirmed with Enter, is shown in canonical form, with its preset selected.
    typeAndEnter(ruleText, " s34678/b3678 ");
    EXPECT_EQ(m_world.rule().toString(), "B3678/S34678");
    EXPECT_EQ(m_panel->ruleText(), "B3678/S34678");
    EXPECT_EQ(toUtf8(presets.GetStringSelection()), "Day & Night");

    // A rule without a preset, applied with the button, shows "Custom"; choosing "Custom" does
    // nothing.
    ruleText.ChangeValue("B2/S34");
    click(labelled<wxButton>(*m_panel, "Apply"));
    EXPECT_EQ(m_world.rule().toString(), "B2/S34");
    EXPECT_EQ(toUtf8(presets.GetStringSelection()), "Custom");
    command(RulePresetID);
    EXPECT_EQ(m_world.rule().toString(), "B2/S34");

    // Invalid text leaves the rule alone, stays in the box for fixing, and the error line explains
    // it.
    struct BadRule
    {
        std::string_view text;
        core::RuleError  error;
    };
    const std::array badRules{
        BadRule{.text = "B9/S23", .error = core::RuleError::NeighbourOutOfRange},
        BadRule{.text = "Life", .error = core::RuleError::Syntax},
        BadRule{.text = "   ", .error = core::RuleError::Empty}};
    auto const& errorLine = first<wxStaticText>(group("Rule"));  // the group's only text
    for (const BadRule& bad : badRules)
    {
        typeAndEnter(ruleText, bad.text);
        EXPECT_EQ(m_world.rule().toString(), "B2/S34");
        EXPECT_EQ(m_panel->ruleText(), bad.text);
        EXPECT_TRUE(errorLine.IsShown());
        std::string message = toUtf8(errorLine.GetLabelText());
        std::ranges::replace(message, '\n', ' ');  // the panel wraps long messages
        EXPECT_EQ(message, core::describe(bad.error));
    }
    typeAndEnter(ruleText, "B3/S23");
    EXPECT_EQ(m_world.rule(), core::Rule{});
    EXPECT_FALSE(errorLine.IsShown());
    EXPECT_EQ(m_panel->selectedPreset(), 0U);

    // Edit Rule… moves the keyboard focus into the text box and selects the text.
    command(FocusRuleID);
    EXPECT_EQ(wxWindow::FindFocus(), &ruleText);
    EXPECT_EQ(toUtf8(ruleText.GetStringSelection()), "B3/S23");

    // Apply keeps the focus in the rule box. Other panel buttons and check boxes hand it to the
    // canvas: GTK leaves it on the clicked control, and Space would then press that control again.
    click(labelled<wxButton>(*m_panel, "Apply"));
    EXPECT_EQ(wxWindow::FindFocus(), &ruleText);
    toggle(labelled<wxCheckBox>(*m_panel, "Wrap edges"));
    EXPECT_EQ(wxWindow::FindFocus(), m_canvas);
    command(FocusRuleID);
    click(labelled<wxButton>(*m_panel, "Randomize"));
    EXPECT_EQ(wxWindow::FindFocus(), m_canvas);
}

TEST_F(GuiSmokeTest, SwitchesEngines)
{
    command(EngineReferenceID);
    EXPECT_EQ(m_world.stepper().kind(), core::StepperKind::Reference);
    EXPECT_TRUE(menuItem(EngineReferenceID).IsChecked());
    EXPECT_TRUE(status(StatusField::World).ends_with("· Reference"));
    command(StepID);
    command(StepID);
    EXPECT_EQ(m_world.generation(), 2U);
    EXPECT_EQ(m_world.population(), countedPopulation());

    command(EngineBandedID);
    EXPECT_EQ(m_world.stepper().kind(), core::StepperKind::Banded);
    EXPECT_TRUE(menuItem(EngineBandedID).IsChecked());
    EXPECT_TRUE(status(StatusField::World).ends_with("· Banded"));
    command(StepID);
    EXPECT_EQ(m_world.generation(), 3U);  // the engine switch keeps the state
    EXPECT_EQ(m_world.population(), countedPopulation());
}

TEST_F(GuiSmokeTest, SwitchesAutomata)
{
    command(AutomatonAntID);
    EXPECT_EQ(m_world.automaton(), core::Automaton::LangtonAnt);
    EXPECT_TRUE(menuItem(AutomatonAntID).IsChecked());
    ASSERT_EQ(m_world.ants().size(), 1U);  // switching over puts one ant in the middle
    EXPECT_EQ(m_world.ants()[0], core::defaultAnt(0, 1, m_world.extent()));
    EXPECT_EQ(status(StatusField::World), "512 × 512 · Langton's ant · 1 ant");

    // The ant reads no rule, no topology and no engine, so all three are greyed out.
    EXPECT_FALSE(menuItem(ToggleWrapID).IsEnabled());
    EXPECT_FALSE(menuItem(FocusRuleID).IsEnabled());
    EXPECT_FALSE(menuItem(EngineBandedID).IsEnabled());
    EXPECT_FALSE(menuItem(EngineReferenceID).IsEnabled());
    EXPECT_TRUE(menuItem(ResetAntsID).IsEnabled());
    EXPECT_FALSE(labelled<wxCheckBox>(*m_panel, "Wrap edges").IsEnabled());
    EXPECT_FALSE(group("Rule").IsEnabled());

    const core::CellCount before = m_world.population();
    command(StepID);
    EXPECT_EQ(m_world.generation(), 1U);
    EXPECT_EQ(m_world.population(), countedPopulation());
    EXPECT_NE(m_world.population(), before);  // the ant flipped the cell it stood on
    EXPECT_NE(m_world.ants()[0].position, core::defaultAnt(0, 1, m_world.extent()).position);

    command(AutomatonLifeID);
    EXPECT_EQ(m_world.automaton(), core::Automaton::Life);
    EXPECT_TRUE(menuItem(AutomatonLifeID).IsChecked());
    EXPECT_TRUE(status(StatusField::World).ends_with("· B3/S23 · Banded"));
    EXPECT_TRUE(menuItem(ToggleWrapID).IsEnabled());
    EXPECT_TRUE(menuItem(EngineBandedID).IsEnabled());
    EXPECT_TRUE(group("Rule").IsEnabled());
    command(StepID);
    EXPECT_EQ(m_world.generation(), 2U);  // the switch keeps the state
    EXPECT_EQ(m_world.population(), countedPopulation());
}

TEST_F(GuiSmokeTest, PlacesAntsFromTheMenuAndWithCtrlClick)
{
    command(AutomatonAntID);
    // The ant count is the group's second spin control; the first is the randomize density.
    const std::vector<wxSpinCtrl*> spins = all<wxSpinCtrl>(group("Simulation"));
    ASSERT_EQ(spins.size(), 2U);
    wxSpinCtrl& antCount = *spins.at(1);

    type(antCount, 3);
    ASSERT_EQ(m_world.ants().size(), 3U);
    for (std::size_t i = 0; i < 3; ++i)
    {
        EXPECT_EQ(m_world.ants()[i], core::defaultAnt(static_cast<int>(i), 3, m_world.extent()))
            << "ant " << i;
    }
    EXPECT_EQ(status(StatusField::World), "512 × 512 · Langton's ant · 3 ants");

    command(StepID);
    EXPECT_NE(m_world.ants()[0], core::defaultAnt(0, 3, m_world.extent()));
    command(ResetAntsID);  // Edit -> Reset Ants puts them back without changing how many there are
    ASSERT_EQ(m_world.ants().size(), 3U);
    EXPECT_EQ(m_world.ants()[0], core::defaultAnt(0, 3, m_world.extent()));

    // Ctrl+left click adds an ant where it points and never draws a cell. The ants share the middle
    // row, so a cell below it is free and the click adds one instead of taking that one away.
    const wxPoint                      at   = canvasCentre() + wxPoint(0, 40);
    const std::optional<core::CellPos> cell = pointAt(at);
    ASSERT_TRUE(cell.has_value());
    ASSERT_FALSE(std::ranges::any_of(
        m_world.ants(), [&](const core::Ant& ant) { return ant.position == cell.value(); }));
    const core::CellCount population = m_world.population();
    mouse(*m_canvas, wxEVT_LEFT_DOWN, at, wxMOD_CONTROL);
    mouse(*m_canvas, wxEVT_LEFT_UP, at, wxMOD_CONTROL);
    ASSERT_EQ(m_world.ants().size(), 4U);
    EXPECT_EQ(m_world.ants()[3], (core::Ant{cell.value(), core::Heading::North}));
    EXPECT_EQ(m_world.population(), population);  // no stroke was drawn
    EXPECT_EQ(wxWindow::GetCapture(), nullptr);   // and no drag was started
    EXPECT_EQ(antCount.GetValue(), 4);            // the panel follows the model
    EXPECT_EQ(status(StatusField::World), "512 × 512 · Langton's ant · 4 ants");

    // Clicking the same cell again takes it away.
    mouse(*m_canvas, wxEVT_LEFT_DOWN, at, wxMOD_CONTROL);
    mouse(*m_canvas, wxEVT_LEFT_UP, at, wxMOD_CONTROL);
    EXPECT_EQ(m_world.ants().size(), 3U);
    EXPECT_EQ(m_world.population(), population);
}

TEST_F(GuiSmokeTest, ResizesTheWorldThroughTheSizeDialog)
{
    DialogAnswers answers;
    command(EngineReferenceID);
    command(RunPauseID);

    // Grow to 1001 × 1000, keeping the pattern. That is too large for the Reference engine.
    const core::CellCount population = m_world.population();
    const std::uint64_t   generation = m_world.generation();
    answers.worldSize                = DialogAnswers::SizeEntry{.width = "1001", .height = "1000"};
    command(WorldSizeID);
    EXPECT_EQ(answers.titles, std::vector<std::string>{"World Size"});
    EXPECT_EQ(m_world.extent(), (core::Extent{1001, 1000}));
    EXPECT_EQ(m_world.population(), population);
    EXPECT_EQ(m_world.stepper().kind(), core::StepperKind::Banded);
    EXPECT_TRUE(menuItem(EngineBandedID).IsChecked());
    EXPECT_FALSE(menuItem(EngineReferenceID).IsEnabled());
    EXPECT_EQ(status(StatusField::World), "1,001 × 1,000 · torus · B3/S23 · Banded");
    const std::string worldInfo =
        "1,001 × 1,000 cells\n" + core::formatBytes(core::worldBytes(m_world.extent()));
    EXPECT_NE(wxWindow::FindWindowByLabel(toWx(worldInfo), m_panel), nullptr);
    EXPECT_EQ(m_canvas->cellSize(), fittedCellSize());
    repaint();

    // The simulation keeps running in the larger world.
    EXPECT_EQ(status(StatusField::State), "Running");
    EXPECT_TRUE(runUntil([&] { return m_world.generation() > generation; }));

    // The Reference engine stays off while the world is too large for it.
    command(EngineReferenceID);
    EXPECT_EQ(m_world.stepper().kind(), core::StepperKind::Banded);
    EXPECT_TRUE(menuItem(EngineBandedID).IsChecked());

    // Cancel changes nothing, and neither does an invalid size: the dialog explains it, disables OK
    // and refuses Enter. The typed text counts, not the value a number box would clamp it to.
    answers.worldSize.reset();
    command(WorldSizeID);
    const core::Extent largest{.width = core::kMaxWorldSide, .height = core::kMaxWorldSide};
    const std::string  tooSmall = core::describe(core::ExtentError::TooSmall, {}, 0);
    const std::string  notWhole = "Width and height must be whole numbers.";
    const std::array<std::array<std::string, 3>, 6> invalidSizes{{
        {std::to_string(largest.width), std::to_string(largest.height),
         core::describe(core::ExtentError::OverMemoryBudget, largest, core::defaultMemoryBudget())},
        {"250000", "10", core::describe(core::ExtentError::TooLarge, {}, 0)},
        {"0", "10", tooSmall},
        {"10", "-40", tooSmall},
        {"", "10", notWhole},
        {"10", "abc", notWhole},  // a number box takes no letters, so it stays empty
    }};
    for (const auto& [width, height, message] : invalidSizes)
    {
        answers.worldSize = DialogAnswers::SizeEntry{.width = width, .height = height};
        command(WorldSizeID);
        EXPECT_TRUE(std::ranges::contains(answers.sizeTexts, message)) << width << " × " << height;
    }
    EXPECT_EQ(answers.titles.size(), 2 + invalidSizes.size());
    EXPECT_EQ(m_world.extent(), (core::Extent{1001, 1000}));

    // The smallest world, without the pattern: empty, generation 0, and fitted at the largest cell
    // size. The status bar shows the one cell, now under the resting pointer.
    command(RunPauseID);
    pointAt(canvasCentre());
    answers.worldSize = DialogAnswers::SizeEntry{.width = "1", .height = "1", .keepPattern = false};
    click(labelled<wxButton>(*m_panel, "Resize…"));
    EXPECT_EQ(m_world.extent(), (core::Extent{1, 1}));
    EXPECT_EQ(m_world.population(), 0);
    EXPECT_EQ(m_world.generation(), 0U);
    EXPECT_EQ(status(StatusField::State), "Paused");
    EXPECT_EQ(m_canvas->cellSize(), render::kMaxCellSize);
    EXPECT_EQ(hoveredCell(), (core::CellPos{0, 0}));
    repaint();
    mouse(*m_canvas, wxEVT_LEFT_DOWN, canvasCentre());
    mouse(*m_canvas, wxEVT_LEFT_UP, canvasCentre());
    EXPECT_EQ(m_world.population(), 1);

    // Reference is allowed again. On a 1 × 1 torus the cell is its own eight neighbours, so it
    // dies.
    command(EngineReferenceID);
    EXPECT_EQ(m_world.stepper().kind(), core::StepperKind::Reference);
    command(StepID);
    EXPECT_EQ(m_world.population(), 0);

    // The keyboard help is a modal message box too.
    command(ShowControlsHelpID);
    EXPECT_EQ(answers.titles.back(), "Keyboard and Mouse");
}

TEST_F(GuiSmokeTest, QuitsCleanlyInTheMiddleOfAStroke)
{
    command(RunPauseID);
    runFor(100ms);
    mouse(*m_canvas, wxEVT_LEFT_DOWN, canvasCentre());
    ASSERT_EQ(wxWindow::GetCapture(), m_canvas);

    // File → Quit. The frame lets go of the mouse at once; wx deletes the frame when the loop is
    // idle.
    wxCommandEvent quit(wxEVT_MENU, wxID_EXIT);
    deliver(*m_frame, quit);
    EXPECT_EQ(wxWindow::GetCapture(), nullptr);
    EXPECT_TRUE(runUntil([this] { return !m_frame; }));

    // The World outlives the frame, and nothing steps it any more.
    const std::uint64_t generation = m_world.generation();
    runFor(100ms);
    EXPECT_EQ(m_world.generation(), generation);
    EXPECT_EQ(m_world.population(), countedPopulation());
}

}  // namespace
}  // namespace wxLife::ui

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
    static std::vector<std::string> list;
    return list;
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
    static bool active() { return active_; }
    /// A display is configured, but wx did not start.
    static bool startFailed() { return startFailed_; }
    static bool drawsWindows() { return drawsWindows_; }
    static bool recheckDrawing() { return drawsWindows_ = displayDrawsWindows(); }

    /// Starts wx on the first call; later calls do nothing, because GTK cannot be started twice.
    static void start()
    {
        if (std::exchange(started_, true) || !displayConfigured())
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
            startFailed_ = true;
            return;
        }
        active_ = true;
        if (!wxTheApp->CallOnInit())
        {
            startFailed_ = true;
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
        if (!active_)
        {
            return;
        }
        wxTheApp->OnExit();
        wxEntryCleanup();
        active_ = false;
    }

private:
    static inline bool started_      = false;
    static inline bool active_       = false;
    static inline bool startFailed_  = false;
    static inline bool drawsWindows_ = false;
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
        kState,
        kGeneration,
        kPopulation,
        kSpeed,
        kWorld,
        kView
    };
    using enum StatusField;

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

        frame_  = new MainFrame(world_);  // wx deletes it after it is closed
        canvas_ = &first<WorldCanvas>(*frame_);
        panel_  = &first<ControlPanel>(*frame_);
        // Handlers bound later run first, so this counts every paint and Skip() lets the canvas
        // draw it.
        canvas_->Bind(wxEVT_PAINT, [this](wxPaintEvent& event) {
            ++paints_;
            event.Skip();
        });
        // Without activation, keys pressed by the person at the desktop go where they did.
        frame_->ShowWithoutActivating();
        paintedSince(0);
    }

    void TearDown() override
    {
        if (!WxSession::active())
        {
            return;
        }
        if (frame_.get() != nullptr)
        {
            frame_->Close();
            EXPECT_TRUE(runUntil([this] { return !frame_; }))
                << "The closed frame was not destroyed.";
            delete frame_.get();  // only left if the check failed; it must not outlive world_
        }
        EXPECT_EQ(wxWindow::GetCapture(), nullptr);
        for (const std::string& problem : std::exchange(problems(), {}))
        {
            ADD_FAILURE() << problem;
        }
    }

    // What a menu item, its accelerator or a canvas key sends.
    void command(CommandId id) { emitCommand(*frame_, id); }

    [[nodiscard]] wxMenuItem& menuItem(CommandId id) const
    {
        return *frame_->GetMenuBar()->FindItem(id);
    }
    [[nodiscard]] std::string menuLabel(CommandId id) const
    {
        return toUtf8(menuItem(id).GetItemLabelText());
    }

    [[nodiscard]] std::string status(StatusField field) const
    {
        return toUtf8(frame_->GetStatusBar()->GetStatusText(std::to_underlying(field)));
    }

    [[nodiscard]] wxStaticBox& group(std::string_view label) const
    {
        return labelled<wxStaticBox>(*panel_, label);
    }

    [[nodiscard]] wxPoint canvasCentre() const
    {
        const wxSize size = canvas_->GetClientSize();
        return {size.x / 2, size.y / 2};
    }

    // The cell the status bar shows under the pointer (nullopt for "–").
    [[nodiscard]] std::optional<core::CellPos> hoveredCell() const
    {
        static const std::regex kCell(R"(^\((-?[0-9]+), (-?[0-9]+)\))");
        const std::string       text = status(kView);
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
        mouse(*canvas_, wxEVT_MOTION, at);
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
        if (runUntil([&] { return paints_ > before; }))
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
        const int before = paints_;
        canvas_->Refresh(false);
        canvas_->Update();
        return paintedSince(before);
    }

    // The cell size that shows the whole world on the current canvas
    // (render::Viewport::fitWorld()).
    [[nodiscard]] int fittedCellSize() const
    {
        const wxSize       logical = canvas_->GetClientSize();
        const double       scale   = canvas_->GetContentScaleFactor();
        const core::Extent world   = world_.extent();
        const long         fit     = std::min(std::lround(logical.x * scale) / world.width,
                                              std::lround(logical.y * scale) / world.height);
        return static_cast<int>(std::clamp<long>(fit, render::kMinCellSize, render::kMaxCellSize));
    }

    [[nodiscard]] core::CellCount countedPopulation() const { return world_.cells().countAlive(); }

    [[nodiscard]] static std::string countText(std::int64_t n)
    {
        return core::formatCount(static_cast<std::uint64_t>(n));
    }

    core::World          world_{defaults::kWorldExtent, core::Rule{}, defaults::kTopology};
    wxWeakRef<MainFrame> frame_;  ///< Becomes null once wx has deleted the frame.
    WorldCanvas*         canvas_ = nullptr;
    ControlPanel*        panel_  = nullptr;
    int                  paints_ = 0;
};

TEST_F(GuiSmokeTest, OpensPausedWithARandomWorldFitted)
{
    // MainFrame's constructor fills 25% of the world at random (the panel's density).
    EXPECT_EQ(world_.generation(), 0U);
    EXPECT_NEAR(
        static_cast<double>(world_.population()) / static_cast<double>(world_.extent().cellCount()),
        0.25, 0.01);
    EXPECT_EQ(world_.population(), countedPopulation());

    EXPECT_EQ(status(kState), "Paused");
    EXPECT_EQ(status(kGeneration), "Gen 0");
    EXPECT_EQ(status(kPopulation), "Pop " + countText(world_.population()));
    EXPECT_EQ(status(kSpeed), "30 gen/s");
    EXPECT_EQ(status(kWorld), "512 × 512 · torus · B3/S23 · Banded");

    // The world is fitted to the canvas. The canvas notices a late change of its size or display
    // scale when it paints, so this is checked after a paint.
    if (repaint())
    {
        EXPECT_EQ(canvas_->cellSize(), fittedCellSize());
    }
    EXPECT_EQ(panel_->cellSize(), canvas_->cellSize());
    EXPECT_TRUE(canvas_->showGrid());
    EXPECT_EQ(panel_->speed(), defaults::kSpeed);
    EXPECT_EQ(panel_->ruleText(), "B3/S23");
    EXPECT_EQ(panel_->selectedPreset(), 0U);  // Conway's Life

    EXPECT_EQ(menuLabel(ID_RUN_PAUSE), "Run");
    EXPECT_FALSE(menuItem(ID_TOGGLE_MAX_SPEED).IsChecked());
    EXPECT_TRUE(menuItem(ID_ENGINE_BANDED).IsChecked());
    EXPECT_TRUE(menuItem(ID_ENGINE_REFERENCE).IsEnabled());
    EXPECT_TRUE(menuItem(ID_TOGGLE_WRAP).IsChecked());
    EXPECT_TRUE(menuItem(ID_TOGGLE_GRID).IsChecked());

    // Life runs until the user asks for the other automaton, and it has no ants.
    EXPECT_EQ(world_.automaton(), core::Automaton::Life);
    EXPECT_TRUE(world_.ants().empty());
    EXPECT_TRUE(menuItem(ID_AUTOMATON_LIFE).IsChecked());
    EXPECT_FALSE(menuItem(ID_RESET_ANTS).IsEnabled());
}

TEST_F(GuiSmokeTest, RunsPausesAndSteps)
{
    // Run for about half a second at the default 30 generations per second.
    const Clock::time_point started      = Clock::now();
    const int               paintsBefore = paints_;
    command(ID_RUN_PAUSE);
    EXPECT_EQ(status(kState), "Running");
    EXPECT_EQ(status(kSpeed), "30 gen/s");  // only the target until a rate has been measured
    EXPECT_EQ(menuLabel(ID_RUN_PAUSE), "Pause");
    EXPECT_NO_THROW(labelled<wxButton>(*panel_, "Pause"));
    runFor(500ms);
    EXPECT_TRUE(runUntil([this] { return world_.generation() > 0; }));
    const std::regex measured(R"(30 gen/s \([0-9]+\.[0-9]\))");  // target and measured rate
    EXPECT_TRUE(runUntil([&] { return std::regex_match(status(kSpeed), measured); }))
        << status(kSpeed);
    command(ID_RUN_PAUSE);
    const double seconds = std::chrono::duration<double>(Clock::now() - started).count();

    // The pacer never runs ahead of the target rate, and the ticks repaint the canvas (several
    // paints, not just one that was still pending).
    const std::uint64_t generation = world_.generation();
    EXPECT_LE(static_cast<double>(generation), (seconds * defaults::kSpeed.gensPerSecond) + 1);
    paintedSince(paintsBefore + 2);
    EXPECT_EQ(world_.population(), countedPopulation());
    EXPECT_EQ(status(kState), "Paused");
    EXPECT_EQ(status(kGeneration), "Gen " + core::formatCount(generation));
    EXPECT_EQ(status(kPopulation), "Pop " + countText(world_.population()));
    EXPECT_EQ(menuLabel(ID_RUN_PAUSE), "Run");

    // Paused means paused.
    runFor(200ms);
    EXPECT_EQ(world_.generation(), generation);

    // One generation at a time: the menu command, the panel button, and N on the canvas.
    command(ID_STEP);
    click(labelled<wxButton>(*panel_, "Step"));
    pressKey(*canvas_, 'N');
    EXPECT_EQ(world_.generation(), generation + 3);
    EXPECT_EQ(status(kGeneration), "Gen " + core::formatCount(generation + 3));
    EXPECT_EQ(world_.population(), countedPopulation());

    // Space and the panel button toggle too. Step while running pauses first.
    pressKey(*canvas_, WXK_SPACE);
    EXPECT_EQ(status(kState), "Running");
    command(ID_STEP);
    EXPECT_EQ(status(kState), "Paused");
    EXPECT_EQ(world_.generation(), generation + 4);
    click(labelled<wxButton>(*panel_, "Run"));
    EXPECT_EQ(status(kState), "Running");
    click(labelled<wxButton>(*panel_, "Pause"));
    EXPECT_EQ(status(kState), "Paused");
}

TEST_F(GuiSmokeTest, ClearsRandomizesAndDrawsWithTheMouse)
{
    command(ID_CLEAR);
    EXPECT_EQ(world_.population(), 0);
    EXPECT_EQ(status(kPopulation), "Pop 0");

    // Randomize reads the density spin control, which sends nothing itself.
    auto& density = first<wxSpinCtrl>(group("Simulation"));
    density.SetValue(100);
    command(ID_RANDOMIZE);
    EXPECT_EQ(world_.population(), world_.extent().cellCount());
    density.SetValue(10);
    click(labelled<wxButton>(*panel_, "Randomize"));
    EXPECT_NEAR(
        static_cast<double>(world_.population()) / static_cast<double>(world_.extent().cellCount()),
        0.10, 0.01);
    EXPECT_EQ(world_.population(), countedPopulation());
    EXPECT_EQ(world_.generation(), 0U);
    click(labelled<wxButton>(*panel_, "Clear"));
    EXPECT_EQ(world_.population(), 0);

    // At 8 px a short drag crosses several cells. A left drag from a dead cell draws a line.
    type(first<wxSpinCtrl>(group("View")), 8);
    ASSERT_EQ(canvas_->cellSize(), 8);
    const wxPoint                      start     = canvasCentre();
    const wxPoint                      end       = start + wxPoint(40, 16);
    const std::optional<core::CellPos> startCell = pointAt(start);
    ASSERT_TRUE(startCell);
    mouse(*canvas_, wxEVT_LEFT_DOWN, start);
    EXPECT_EQ(world_.at(startCell.value()), core::kAlive);
    EXPECT_EQ(wxWindow::GetCapture(), canvas_);
    const std::optional<core::CellPos> endCell = pointAt(end);
    ASSERT_TRUE(endCell);
    mouse(*canvas_, wxEVT_LEFT_UP, end);
    EXPECT_EQ(wxWindow::GetCapture(), nullptr);

    // The line has no gaps: one cell per step along its longer axis.
    const core::CellCount length = std::max(std::abs(endCell.value().x - startCell.value().x),
                                            std::abs(endCell.value().y - startCell.value().y)) +
                                   1;
    EXPECT_GT(length, 2);
    EXPECT_EQ(world_.population(), length);
    EXPECT_EQ(world_.at(endCell.value()), core::kAlive);
    EXPECT_EQ(status(kPopulation), "Pop " + countText(length));

    // A left press on a live cell erases it; the right button always erases.
    mouse(*canvas_, wxEVT_LEFT_DOWN, end);
    mouse(*canvas_, wxEVT_LEFT_UP, end);
    EXPECT_EQ(world_.at(endCell.value()), core::kDead);
    mouse(*canvas_, wxEVT_RIGHT_DOWN, start);
    pointAt(end);
    mouse(*canvas_, wxEVT_RIGHT_UP, end);
    EXPECT_EQ(world_.population(), 0);

    // Esc ends a stroke and releases the mouse.
    mouse(*canvas_, wxEVT_LEFT_DOWN, start);
    pressKey(*canvas_, WXK_ESCAPE);
    EXPECT_EQ(wxWindow::GetCapture(), nullptr);
    pointAt(end);
    EXPECT_EQ(world_.population(), 1);

    // Shift+left drag pans: the cell under the pointer moves along with it, and nothing is drawn.
    mouse(*canvas_, wxEVT_LEFT_DOWN, start, wxMOD_SHIFT);
    pointAt(end);
    mouse(*canvas_, wxEVT_LEFT_UP, end);
    EXPECT_EQ(pointAt(end), startCell);
    EXPECT_EQ(world_.population(), 1);

    // Only the button that started a drag ends it: a right click during a left drag changes
    // nothing.
    command(ID_CLEAR);
    const std::optional<core::CellPos> from = pointAt(start);
    mouse(*canvas_, wxEVT_LEFT_DOWN, start);
    mouse(*canvas_, wxEVT_RIGHT_DOWN, start);
    mouse(*canvas_, wxEVT_RIGHT_UP, start);
    EXPECT_EQ(wxWindow::GetCapture(), canvas_);
    const std::optional<core::CellPos> to = pointAt(end);
    mouse(*canvas_, wxEVT_LEFT_UP, end);
    ASSERT_TRUE(from && to);
    EXPECT_EQ(world_.population(), std::max(std::abs(to.value().x - from.value().x),
                                            std::abs(to.value().y - from.value().y)) +
                                       1);

    // When the camera moves during a stroke, the stroke goes on from the cell now under the
    // pointer, with no line across the jump.
    command(ID_CLEAR);
    const wxPoint                      aside   = canvasCentre() + wxPoint(-100, -60);
    const std::optional<core::CellPos> pressed = pointAt(aside);
    mouse(*canvas_, wxEVT_LEFT_DOWN, aside);
    pressKey(*canvas_, 'F');  // fit: the pointer is now over a cell far away
    const std::optional<core::CellPos> jumped = pointAt(aside + wxPoint(1, 0));
    mouse(*canvas_, wxEVT_LEFT_UP, aside + wxPoint(1, 0));
    ASSERT_TRUE(pressed && jumped);
    EXPECT_GT(std::abs(jumped.value().x - pressed.value().x), 1);
    EXPECT_EQ(world_.population(), 2);
    EXPECT_EQ(world_.at(jumped.value()), core::kAlive);
}

TEST_F(GuiSmokeTest, ChangesSpeed)
{
    command(ID_FASTER);
    EXPECT_EQ(panel_->speed().gensPerSecond, 60);
    EXPECT_EQ(status(kSpeed), "60 gen/s");
    command(ID_SLOWER);
    typeChar(*canvas_, '[');
    EXPECT_EQ(panel_->speed().gensPerSecond, 20);
    // The canvas matches these keys by the typed character. On a German layout ']' is AltGr+9,
    // whose key-down event says '9'.
    pressKey(*canvas_, '9', wxMOD_ALTGR);
    typeChar(*canvas_, ']', wxMOD_ALTGR);
    EXPECT_EQ(panel_->speed().gensPerSecond, 30);
    typeChar(*canvas_, ']', wxMOD_ALT);  // a plain Alt combination is left to the menus
    EXPECT_EQ(panel_->speed().gensPerSecond, 30);

    // The spin control and the logarithmic slider show the same rate.
    auto& rate   = first<wxSpinCtrl>(group("Speed"));
    auto& slider = first<wxSlider>(group("Speed"));
    type(rate, 1000);
    EXPECT_EQ(slider.GetValue(), core::Speed::kSliderMax);
    EXPECT_EQ(status(kSpeed), "1000 gen/s");
    drag(slider, 100);  // 10^(100 / 100)
    EXPECT_EQ(rate.GetValue(), 10);
    EXPECT_EQ(status(kSpeed), "10 gen/s");

    // Faster from 1000 reaches Max; Slower leaves it and keeps the rate.
    type(rate, 1000);
    command(ID_FASTER);
    EXPECT_TRUE(panel_->speed().unlimited);
    EXPECT_TRUE(menuItem(ID_TOGGLE_MAX_SPEED).IsChecked());
    EXPECT_FALSE(rate.IsEnabled());
    EXPECT_EQ(status(kSpeed), "Max");
    command(ID_SLOWER);
    EXPECT_EQ(panel_->speed(), (core::Speed{.gensPerSecond = 1000}));
    EXPECT_TRUE(rate.IsEnabled());

    // The check box switches Max on. The runner then steps several generations per tick.
    type(rate, 30);
    toggle(labelled<wxCheckBox>(*panel_, "Max speed"));
    EXPECT_TRUE(panel_->speed().unlimited);
    command(ID_RUN_PAUSE);
    runFor(300ms);
    EXPECT_GT(world_.generation(), 30U);  // one generation per 16 ms tick would give about 19
    // Once the first half-second window has passed, the status bar shows the measured rate.
    const std::regex measuredMax(R"(Max \([1-9][0-9,]* gen/s\))");
    EXPECT_TRUE(runUntil([&] { return std::regex_match(status(kSpeed), measuredMax); }))
        << status(kSpeed);
    command(ID_RUN_PAUSE);
    EXPECT_EQ(world_.population(), countedPopulation());

    command(ID_TOGGLE_MAX_SPEED);
    EXPECT_EQ(panel_->speed(), (core::Speed{.gensPerSecond = 30}));
    EXPECT_FALSE(menuItem(ID_TOGGLE_MAX_SPEED).IsChecked());
    EXPECT_EQ(status(kSpeed), "30 gen/s");

    // The frame takes Ctrl+M before any control sees it; in the rule box wxGTK would turn it into
    // Enter.
    auto& ruleText = first<wxTextCtrl>(group("Rule"));
    sendKey(ruleText, wxEVT_CHAR_HOOK, 'M', wxMOD_CONTROL);
    EXPECT_TRUE(panel_->speed().unlimited);
    sendKey(ruleText, wxEVT_CHAR_HOOK, 'M', wxMOD_CONTROL | wxMOD_SHIFT);  // only plain Ctrl+M
    EXPECT_TRUE(panel_->speed().unlimited);
    sendKey(*canvas_, wxEVT_CHAR_HOOK, 'M', wxMOD_CONTROL);
    EXPECT_FALSE(panel_->speed().unlimited);
}

TEST_F(GuiSmokeTest, ZoomsScrollsFitsAndCentres)
{
    auto& cellSize   = first<wxSpinCtrl>(group("View"));
    auto& zoomSlider = first<wxSlider>(group("View"));

    // Both ends of the zoom range, painted at each.
    for (std::size_t i = 0; i < render::kZoomSteps.size(); ++i)
    {
        command(ID_ZOOM_OUT);
    }
    EXPECT_EQ(canvas_->cellSize(), render::kMinCellSize);
    EXPECT_EQ(cellSize.GetValue(), render::kMinCellSize);
    EXPECT_TRUE(status(kView).ends_with("· 1 px · grid hidden < 5 px")) << status(kView);
    repaint();
    for (std::size_t i = 0; i < render::kZoomSteps.size(); ++i)
    {
        command(ID_ZOOM_IN);
    }
    EXPECT_EQ(canvas_->cellSize(), render::kMaxCellSize);
    EXPECT_EQ(cellSize.GetValue(), render::kMaxCellSize);
    EXPECT_EQ(zoomSlider.GetValue(), static_cast<int>(render::kZoomSteps.size() - 1));
    EXPECT_TRUE(status(kView).ends_with("· 100 px")) << status(kView);
    repaint();

    // Centre: the middle cell is under the canvas centre, and both scrollbars sit halfway.
    command(ID_CENTER_VIEW);
    const std::optional<core::CellPos> middle = pointAt(canvasCentre());
    ASSERT_TRUE(middle);
    EXPECT_NEAR(middle.value().x, 256, 1);
    EXPECT_NEAR(middle.value().y, 256, 1);
    for (const int orientation : {wxHORIZONTAL, wxVERTICAL})
    {
        const int range = canvas_->GetScrollRange(orientation);  // device pixels, like the thumb
        EXPECT_EQ(range, 512 * render::kMaxCellSize);
        EXPECT_EQ(canvas_->GetScrollPos(orientation),
                  (range - canvas_->GetScrollThumb(orientation)) / 2);
    }

    // Scrollbar events, then arrow keys, move the view.
    const int end = canvas_->GetScrollRange(wxVERTICAL) - canvas_->GetScrollThumb(wxVERTICAL);
    scroll(*canvas_, wxEVT_SCROLLWIN_BOTTOM, wxVERTICAL);
    scroll(*canvas_, wxEVT_SCROLLWIN_TOP, wxHORIZONTAL);
    EXPECT_EQ(canvas_->GetScrollPos(wxVERTICAL), end);
    EXPECT_EQ(canvas_->GetScrollPos(wxHORIZONTAL), 0);
    const wxPoint bottomLeft{0, canvas_->GetClientSize().y - 1};
    EXPECT_EQ(pointAt(bottomLeft), (core::CellPos{0, 511}));  // the world's bottom-left cell
    scroll(*canvas_, wxEVT_SCROLLWIN_THUMBTRACK, wxHORIZONTAL, 1000);
    EXPECT_EQ(canvas_->GetScrollPos(wxHORIZONTAL), 1000);
    pressKey(*canvas_, WXK_RIGHT);  // 10% of the canvas width
    EXPECT_EQ(canvas_->GetScrollPos(wxHORIZONTAL),
              1000 + (canvas_->GetScrollThumb(wxHORIZONTAL) / 10));
    pressKey(*canvas_, WXK_PAGEUP);
    EXPECT_LT(canvas_->GetScrollPos(wxVERTICAL), end);

    // Ctrl+Home centres, handled by the canvas itself: GTK's scrolled window would take it from the
    // menu.
    pressKey(*canvas_, WXK_HOME, wxMOD_CONTROL);
    for (const int orientation : {wxHORIZONTAL, wxVERTICAL})
    {
        const int centred =
            (canvas_->GetScrollRange(orientation) - canvas_->GetScrollThumb(orientation)) / 2;
        EXPECT_EQ(canvas_->GetScrollPos(orientation), centred);
    }

    // Fit shows the whole world, from the menu, the F key and the panel button alike.
    command(ID_ZOOM_FIT);
    const int fitted = fittedCellSize();
    EXPECT_EQ(canvas_->cellSize(), fitted);
    typeChar(*canvas_, '+');
    const int zoomedIn = canvas_->cellSize();
    EXPECT_GT(zoomedIn, fitted);
    typeChar(*canvas_, '=');
    typeChar(*canvas_, '-');
    EXPECT_EQ(canvas_->cellSize(), zoomedIn);
    pressKey(*canvas_, 'F');
    EXPECT_EQ(canvas_->cellSize(), fitted);
    pressKey(*canvas_, WXK_NUMPAD_ADD);
    click(labelled<wxButton>(*panel_, "Fit"));
    EXPECT_EQ(canvas_->cellSize(), fitted);
    EXPECT_EQ(cellSize.GetValue(), fitted);

    // The spin control takes any size; the slider moves along the zoom steps.
    type(cellSize, 37);
    EXPECT_EQ(canvas_->cellSize(), 37);
    EXPECT_EQ(zoomSlider.GetValue(), static_cast<int>(render::nearestZoomStep(37)));
    drag(zoomSlider, 3);
    EXPECT_EQ(canvas_->cellSize(), render::kZoomSteps[3]);
    EXPECT_EQ(cellSize.GetValue(), render::kZoomSteps[3]);

    // Ctrl+wheel zooms at the pointer, which stays over the same cell.
    type(cellSize, 16);
    pressKey(*canvas_, 'C');
    const wxPoint                      pointer = canvasCentre() + wxPoint(37, -23);
    const std::optional<core::CellPos> cell    = pointAt(pointer);
    ASSERT_TRUE(cell);
    turnWheel(*canvas_, pointer, 1, wxMOD_CONTROL);
    EXPECT_EQ(canvas_->cellSize(), 20);
    EXPECT_EQ(pointAt(pointer), cell);
    turnWheel(*canvas_, pointer, -2, wxMOD_CONTROL);
    EXPECT_EQ(canvas_->cellSize(), 12);
    EXPECT_EQ(pointAt(pointer), cell);

    // A plain wheel notch down scrolls 3 lines of max(cell size, 16) px.
    const int top = canvas_->GetScrollPos(wxVERTICAL);
    turnWheel(*canvas_, pointer, -1);
    EXPECT_EQ(canvas_->GetScrollPos(wxVERTICAL), top + (3 * 16));

    // Ctrl and a key belong to the menu accelerators, so the canvas ignores it.
    pressKey(*canvas_, '+', wxMOD_CONTROL);
    typeChar(*canvas_, '+', wxMOD_CONTROL);
    EXPECT_EQ(canvas_->cellSize(), 12);

    // When the view moves under a resting pointer, the status bar shows the cell now under it.
    const auto expectHoverFollows = [&](const std::function<void()>& moveView) {
        const std::optional<core::CellPos> before = pointAt(pointer);
        moveView();
        const std::optional<core::CellPos> shown = hoveredCell();
        const std::optional<core::CellPos> under = pointAt(pointer);  // the same spot again
        EXPECT_NE(under, before);
        EXPECT_EQ(shown, under);
    };
    expectHoverFollows([&] { command(ID_ZOOM_IN); });
    expectHoverFollows([&] { pressKey(*canvas_, WXK_DOWN); });
    expectHoverFollows([&] { scroll(*canvas_, wxEVT_SCROLLWIN_PAGEUP, wxVERTICAL); });
}

TEST_F(GuiSmokeTest, TogglesGridLinesAndWrapping)
{
    // Below 5 px the lines would hide the cells, and the status bar says why none are drawn.
    auto& gridLines = labelled<wxCheckBox>(*panel_, "Grid lines");
    type(first<wxSpinCtrl>(group("View")), 4);
    EXPECT_TRUE(status(kView).ends_with("· 4 px · grid hidden < 5 px")) << status(kView);
    command(ID_TOGGLE_GRID);
    EXPECT_FALSE(canvas_->showGrid());
    EXPECT_FALSE(gridLines.GetValue());
    EXPECT_FALSE(menuItem(ID_TOGGLE_GRID).IsChecked());
    EXPECT_TRUE(status(kView).ends_with("· 4 px")) << status(kView);
    pressKey(*canvas_, 'G');
    EXPECT_TRUE(canvas_->showGrid());
    EXPECT_TRUE(menuItem(ID_TOGGLE_GRID).IsChecked());

    // At 20 px, paint once with grid lines and once without.
    type(first<wxSpinCtrl>(group("View")), 20);
    repaint();
    toggle(gridLines);
    EXPECT_FALSE(canvas_->showGrid());
    repaint();

    // A theme-change event keeps the grid setting and leaves the colours of the current theme. (The
    // desktop theme itself does not change here, so this cannot tell whether the colours are picked
    // again.)
    wxSysColourChangedEvent themeChanged;
    deliver(*frame_, themeChanged);
    EXPECT_FALSE(canvas_->showGrid());
    EXPECT_EQ(canvas_->style().outside, themeStyle().outside);
    repaint();

    // Wrap edges switches the world's topology.
    auto& wrap = labelled<wxCheckBox>(*panel_, "Wrap edges");
    command(ID_TOGGLE_WRAP);
    EXPECT_EQ(world_.topology(), core::Topology::Bounded);
    EXPECT_FALSE(wrap.GetValue());
    EXPECT_FALSE(menuItem(ID_TOGGLE_WRAP).IsChecked());
    EXPECT_EQ(status(kWorld), "512 × 512 · bounded · B3/S23 · Banded");
    pressKey(*canvas_, 'W');
    EXPECT_EQ(world_.topology(), core::Topology::Torus);
    EXPECT_TRUE(wrap.GetValue());
    toggle(wrap);
    EXPECT_EQ(world_.topology(), core::Topology::Bounded);
    EXPECT_FALSE(menuItem(ID_TOGGLE_WRAP).IsChecked());
}

TEST_F(GuiSmokeTest, AppliesPresetAndTypedRules)
{
    auto& presets  = first<wxChoice>(group("Rule"));
    auto& ruleText = first<wxTextCtrl>(group("Rule"));

    choose(presets, 1);
    EXPECT_EQ(world_.rule(), core::kRulePresets[1].rule);  // HighLife
    EXPECT_EQ(panel_->ruleText(), "B36/S23");
    EXPECT_EQ(status(kWorld), "512 × 512 · torus · B36/S23 · Banded");

    // Typed text, confirmed with Enter, is shown in canonical form, with its preset selected.
    typeAndEnter(ruleText, " s34678/b3678 ");
    EXPECT_EQ(world_.rule().toString(), "B3678/S34678");
    EXPECT_EQ(panel_->ruleText(), "B3678/S34678");
    EXPECT_EQ(toUtf8(presets.GetStringSelection()), "Day & Night");

    // A rule without a preset, applied with the button, shows "Custom"; choosing "Custom" does
    // nothing.
    ruleText.ChangeValue("B2/S34");
    click(labelled<wxButton>(*panel_, "Apply"));
    EXPECT_EQ(world_.rule().toString(), "B2/S34");
    EXPECT_EQ(toUtf8(presets.GetStringSelection()), "Custom");
    command(ID_RULE_PRESET);
    EXPECT_EQ(world_.rule().toString(), "B2/S34");

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
        EXPECT_EQ(world_.rule().toString(), "B2/S34");
        EXPECT_EQ(panel_->ruleText(), bad.text);
        EXPECT_TRUE(errorLine.IsShown());
        std::string message = toUtf8(errorLine.GetLabelText());
        std::ranges::replace(message, '\n', ' ');  // the panel wraps long messages
        EXPECT_EQ(message, core::describe(bad.error));
    }
    typeAndEnter(ruleText, "B3/S23");
    EXPECT_EQ(world_.rule(), core::Rule{});
    EXPECT_FALSE(errorLine.IsShown());
    EXPECT_EQ(panel_->selectedPreset(), 0U);

    // Edit Rule… moves the keyboard focus into the text box and selects the text.
    command(ID_FOCUS_RULE);
    EXPECT_EQ(wxWindow::FindFocus(), &ruleText);
    EXPECT_EQ(toUtf8(ruleText.GetStringSelection()), "B3/S23");

    // Apply keeps the focus in the rule box. Other panel buttons and check boxes hand it to the
    // canvas: GTK leaves it on the clicked control, and Space would then press that control again.
    click(labelled<wxButton>(*panel_, "Apply"));
    EXPECT_EQ(wxWindow::FindFocus(), &ruleText);
    toggle(labelled<wxCheckBox>(*panel_, "Wrap edges"));
    EXPECT_EQ(wxWindow::FindFocus(), canvas_);
    command(ID_FOCUS_RULE);
    click(labelled<wxButton>(*panel_, "Randomize"));
    EXPECT_EQ(wxWindow::FindFocus(), canvas_);
}

TEST_F(GuiSmokeTest, SwitchesEngines)
{
    command(ID_ENGINE_REFERENCE);
    EXPECT_EQ(world_.stepper().kind(), core::StepperKind::Reference);
    EXPECT_TRUE(menuItem(ID_ENGINE_REFERENCE).IsChecked());
    EXPECT_TRUE(status(kWorld).ends_with("· Reference"));
    command(ID_STEP);
    command(ID_STEP);
    EXPECT_EQ(world_.generation(), 2U);
    EXPECT_EQ(world_.population(), countedPopulation());

    command(ID_ENGINE_BANDED);
    EXPECT_EQ(world_.stepper().kind(), core::StepperKind::Banded);
    EXPECT_TRUE(menuItem(ID_ENGINE_BANDED).IsChecked());
    EXPECT_TRUE(status(kWorld).ends_with("· Banded"));
    command(ID_STEP);
    EXPECT_EQ(world_.generation(), 3U);  // the engine switch keeps the state
    EXPECT_EQ(world_.population(), countedPopulation());
}

TEST_F(GuiSmokeTest, SwitchesAutomata)
{
    command(ID_AUTOMATON_ANT);
    EXPECT_EQ(world_.automaton(), core::Automaton::LangtonAnt);
    EXPECT_TRUE(menuItem(ID_AUTOMATON_ANT).IsChecked());
    ASSERT_EQ(world_.ants().size(), 1U);  // switching over puts one ant in the middle
    EXPECT_EQ(world_.ants()[0], core::defaultAnt(0, 1, world_.extent()));
    EXPECT_EQ(status(kWorld), "512 × 512 · Langton's ant · 1 ant");

    // The ant reads no rule, no topology and no engine, so all three are greyed out.
    EXPECT_FALSE(menuItem(ID_TOGGLE_WRAP).IsEnabled());
    EXPECT_FALSE(menuItem(ID_FOCUS_RULE).IsEnabled());
    EXPECT_FALSE(menuItem(ID_ENGINE_BANDED).IsEnabled());
    EXPECT_FALSE(menuItem(ID_ENGINE_REFERENCE).IsEnabled());
    EXPECT_TRUE(menuItem(ID_RESET_ANTS).IsEnabled());
    EXPECT_FALSE(labelled<wxCheckBox>(*panel_, "Wrap edges").IsEnabled());
    EXPECT_FALSE(group("Rule").IsEnabled());

    const core::CellCount before = world_.population();
    command(ID_STEP);
    EXPECT_EQ(world_.generation(), 1U);
    EXPECT_EQ(world_.population(), countedPopulation());
    EXPECT_NE(world_.population(), before);  // the ant flipped the cell it stood on
    EXPECT_NE(world_.ants()[0].position, core::defaultAnt(0, 1, world_.extent()).position);

    command(ID_AUTOMATON_LIFE);
    EXPECT_EQ(world_.automaton(), core::Automaton::Life);
    EXPECT_TRUE(menuItem(ID_AUTOMATON_LIFE).IsChecked());
    EXPECT_TRUE(status(kWorld).ends_with("· B3/S23 · Banded"));
    EXPECT_TRUE(menuItem(ID_TOGGLE_WRAP).IsEnabled());
    EXPECT_TRUE(menuItem(ID_ENGINE_BANDED).IsEnabled());
    EXPECT_TRUE(group("Rule").IsEnabled());
    command(ID_STEP);
    EXPECT_EQ(world_.generation(), 2U);  // the switch keeps the state
    EXPECT_EQ(world_.population(), countedPopulation());
}

TEST_F(GuiSmokeTest, PlacesAntsFromTheMenuAndWithCtrlClick)
{
    command(ID_AUTOMATON_ANT);
    // The ant count is the group's second spin control; the first is the randomize density.
    const std::vector<wxSpinCtrl*> spins = all<wxSpinCtrl>(group("Simulation"));
    ASSERT_EQ(spins.size(), 2U);
    wxSpinCtrl& antCount = *spins.at(1);

    type(antCount, 3);
    ASSERT_EQ(world_.ants().size(), 3U);
    for (std::size_t i = 0; i < 3; ++i)
    {
        EXPECT_EQ(world_.ants()[i], core::defaultAnt(static_cast<int>(i), 3, world_.extent()))
            << "ant " << i;
    }
    EXPECT_EQ(status(kWorld), "512 × 512 · Langton's ant · 3 ants");

    command(ID_STEP);
    EXPECT_NE(world_.ants()[0], core::defaultAnt(0, 3, world_.extent()));
    command(
        ID_RESET_ANTS);  // Edit -> Reset Ants puts them back without changing how many there are
    ASSERT_EQ(world_.ants().size(), 3U);
    EXPECT_EQ(world_.ants()[0], core::defaultAnt(0, 3, world_.extent()));

    // Ctrl+left click adds an ant where it points and never draws a cell. The ants share the middle
    // row, so a cell below it is free and the click adds one instead of taking that one away.
    const wxPoint                      at   = canvasCentre() + wxPoint(0, 40);
    const std::optional<core::CellPos> cell = pointAt(at);
    ASSERT_TRUE(cell.has_value());
    ASSERT_FALSE(std::ranges::any_of(
        world_.ants(), [&](const core::Ant& ant) { return ant.position == cell.value(); }));
    const core::CellCount population = world_.population();
    mouse(*canvas_, wxEVT_LEFT_DOWN, at, wxMOD_CONTROL);
    mouse(*canvas_, wxEVT_LEFT_UP, at, wxMOD_CONTROL);
    ASSERT_EQ(world_.ants().size(), 4U);
    EXPECT_EQ(world_.ants()[3], (core::Ant{cell.value(), core::Heading::North}));
    EXPECT_EQ(world_.population(), population);  // no stroke was drawn
    EXPECT_EQ(wxWindow::GetCapture(), nullptr);  // and no drag was started
    EXPECT_EQ(antCount.GetValue(), 4);           // the panel follows the model
    EXPECT_EQ(status(kWorld), "512 × 512 · Langton's ant · 4 ants");

    // Clicking the same cell again takes it away.
    mouse(*canvas_, wxEVT_LEFT_DOWN, at, wxMOD_CONTROL);
    mouse(*canvas_, wxEVT_LEFT_UP, at, wxMOD_CONTROL);
    EXPECT_EQ(world_.ants().size(), 3U);
    EXPECT_EQ(world_.population(), population);
}

TEST_F(GuiSmokeTest, ResizesTheWorldThroughTheSizeDialog)
{
    DialogAnswers answers;
    command(ID_ENGINE_REFERENCE);
    command(ID_RUN_PAUSE);

    // Grow to 1001 × 1000, keeping the pattern. That is too large for the Reference engine.
    const core::CellCount population = world_.population();
    const std::uint64_t   generation = world_.generation();
    answers.worldSize                = DialogAnswers::SizeEntry{.width = "1001", .height = "1000"};
    command(ID_WORLD_SIZE);
    EXPECT_EQ(answers.titles, std::vector<std::string>{"World Size"});
    EXPECT_EQ(world_.extent(), (core::Extent{1001, 1000}));
    EXPECT_EQ(world_.population(), population);
    EXPECT_EQ(world_.stepper().kind(), core::StepperKind::Banded);
    EXPECT_TRUE(menuItem(ID_ENGINE_BANDED).IsChecked());
    EXPECT_FALSE(menuItem(ID_ENGINE_REFERENCE).IsEnabled());
    EXPECT_EQ(status(kWorld), "1,001 × 1,000 · torus · B3/S23 · Banded");
    const std::string worldInfo =
        "1,001 × 1,000 cells\n" + core::formatBytes(core::worldBytes(world_.extent()));
    EXPECT_NE(wxWindow::FindWindowByLabel(toWx(worldInfo), panel_), nullptr);
    EXPECT_EQ(canvas_->cellSize(), fittedCellSize());
    repaint();

    // The simulation keeps running in the larger world.
    EXPECT_EQ(status(kState), "Running");
    EXPECT_TRUE(runUntil([&] { return world_.generation() > generation; }));

    // The Reference engine stays off while the world is too large for it.
    command(ID_ENGINE_REFERENCE);
    EXPECT_EQ(world_.stepper().kind(), core::StepperKind::Banded);
    EXPECT_TRUE(menuItem(ID_ENGINE_BANDED).IsChecked());

    // Cancel changes nothing, and neither does an invalid size: the dialog explains it, disables OK
    // and refuses Enter. The typed text counts, not the value a number box would clamp it to.
    answers.worldSize.reset();
    command(ID_WORLD_SIZE);
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
        command(ID_WORLD_SIZE);
        EXPECT_TRUE(std::ranges::contains(answers.sizeTexts, message)) << width << " × " << height;
    }
    EXPECT_EQ(answers.titles.size(), 2 + invalidSizes.size());
    EXPECT_EQ(world_.extent(), (core::Extent{1001, 1000}));

    // The smallest world, without the pattern: empty, generation 0, and fitted at the largest cell
    // size. The status bar shows the one cell, now under the resting pointer.
    command(ID_RUN_PAUSE);
    pointAt(canvasCentre());
    answers.worldSize = DialogAnswers::SizeEntry{.width = "1", .height = "1", .keepPattern = false};
    click(labelled<wxButton>(*panel_, "Resize…"));
    EXPECT_EQ(world_.extent(), (core::Extent{1, 1}));
    EXPECT_EQ(world_.population(), 0);
    EXPECT_EQ(world_.generation(), 0U);
    EXPECT_EQ(status(kState), "Paused");
    EXPECT_EQ(canvas_->cellSize(), render::kMaxCellSize);
    EXPECT_EQ(hoveredCell(), (core::CellPos{0, 0}));
    repaint();
    mouse(*canvas_, wxEVT_LEFT_DOWN, canvasCentre());
    mouse(*canvas_, wxEVT_LEFT_UP, canvasCentre());
    EXPECT_EQ(world_.population(), 1);

    // Reference is allowed again. On a 1 × 1 torus the cell is its own eight neighbours, so it
    // dies.
    command(ID_ENGINE_REFERENCE);
    EXPECT_EQ(world_.stepper().kind(), core::StepperKind::Reference);
    command(ID_STEP);
    EXPECT_EQ(world_.population(), 0);

    // The keyboard help is a modal message box too.
    command(ID_SHOW_CONTROLS_HELP);
    EXPECT_EQ(answers.titles.back(), "Keyboard and Mouse");
}

TEST_F(GuiSmokeTest, QuitsCleanlyInTheMiddleOfAStroke)
{
    command(ID_RUN_PAUSE);
    runFor(100ms);
    mouse(*canvas_, wxEVT_LEFT_DOWN, canvasCentre());
    ASSERT_EQ(wxWindow::GetCapture(), canvas_);

    // File → Quit. The frame lets go of the mouse at once; wx deletes the frame when the loop is
    // idle.
    wxCommandEvent quit(wxEVT_MENU, wxID_EXIT);
    deliver(*frame_, quit);
    EXPECT_EQ(wxWindow::GetCapture(), nullptr);
    EXPECT_TRUE(runUntil([this] { return !frame_; }));

    // The World outlives the frame, and nothing steps it any more.
    const std::uint64_t generation = world_.generation();
    runFor(100ms);
    EXPECT_EQ(world_.generation(), generation);
    EXPECT_EQ(world_.population(), countedPopulation());
}

}  // namespace
}  // namespace wxLife::ui

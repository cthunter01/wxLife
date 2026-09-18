#include "wxLife/app/LifeApp.h"

#include <memory>

#include <wx/utils.h>

#include "wxLife/core/Rule.h"
#include "wxLife/core/World.h"
#include "wxLife/ui/Defaults.h"
#include "wxLife/ui/MainFrame.h"

namespace wxLife::app
{

bool LifeApp::Initialize(int& argCount, wxChar** args)
{
    // WorldCanvas draws exactly the client size wx reports. GTK overlay scrollbars float over the
    // window, so that size would not match the area GTK gives it; classic scrollbars keep the two
    // in step. Set before wxApp::Initialize() starts GTK, and with it other threads: setenv() is
    // not thread-safe. A value the user set wins.
    if (!wxGetEnv("GTK_OVERLAY_SCROLLING", nullptr))
    {
        wxSetEnv("GTK_OVERLAY_SCROLLING", "0");
    }
    return wxApp::Initialize(argCount, args);
}

bool LifeApp::OnInit()
{
    if (!wxApp::OnInit())  // handles --help and rejects unknown options
    {
        return false;
    }
    SetAppName("wxlife");
    SetAppDisplayName("wxLife");

    world_ = std::make_unique<core::World>(ui::defaults::kWorldExtent, core::Rule{},
                                           ui::defaults::kTopology);
    // wx owns the frame and deletes it after it is closed, but always before this object and its
    // World.
    auto* frame = new ui::MainFrame(*world_);  // NOLINT(cppcoreguidelines-owning-memory)
    frame->Show();
    return true;
}

}  // namespace wxLife::app

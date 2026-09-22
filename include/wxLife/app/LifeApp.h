#pragma once

#include <memory>

#include <wx/app.h>

#include "wxLife/core/World.h"

namespace wxLife::app
{

/// The wxApp, which owns the World so that it outlives every window (wx deletes the windows first).
class LifeApp final : public wxApp
{
public:
    /// With wxGTK, sets GTK_OVERLAY_SCROLLING=0 unless it is already set. Then starts wx.
    bool Initialize(int& argCount, wxChar** args) override;
    /// Creates the World and the main frame.
    bool OnInit() override;

private:
    std::unique_ptr<core::World> m_world;
};

}  // namespace wxLife::app

wxDECLARE_APP(wxLife::app::LifeApp);

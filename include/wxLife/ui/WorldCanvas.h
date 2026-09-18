#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/window.h>

#include "wxLife/core/Types.h"
#include "wxLife/core/World.h"
#include "wxLife/render/PixelBuffer.h"
#include "wxLife/render/Rasterizer.h"
#include "wxLife/render/RenderStyle.h"
#include "wxLife/render/Types.h"
#include "wxLife/render/Viewport.h"

namespace wxLife::ui
{

/// Shows the World and turns mouse and keyboard input into strokes, camera moves and commands.
/// It never changes the World: strokes go to MainFrame through Callbacks::paintCells.
class WorldCanvas final : public wxWindow
{
public:
    /// How the canvas reports to its owner (MainFrame).
    struct Callbacks
    {
        /// Apply a stroke segment.
        std::function<void(std::span<const core::CellPos>, core::Cell)> paintCells;
        /// Ctrl + left click on a cell.
        std::function<void(core::CellPos)> toggleAnt;
        /// Zoom, scroll or resize.
        std::function<void()> viewChanged;
        /// nullopt = the pointer is not over the world.
        std::function<void(std::optional<core::CellPos>)> hoverChanged;
    };

    /// `world` is owned by LifeApp and outlives this window.
    WorldCanvas(wxWindow* parent, const core::World& world, Callbacks callbacks);

    /// Device pixels.
    [[nodiscard]] int cellSize() const noexcept;
    /// Anchored at the canvas centre.
    void setCellSize(int px);
    /// Along render::kZoomSteps, anchored at the canvas centre.
    void zoomBy(int steps);
    /// Fits the world into the canvas and keeps it fitted through canvas size changes until the
    /// user zooms or scrolls.
    void               fitWorld();
    void               centerWorld();
    [[nodiscard]] bool showGrid() const noexcept;
    void               setShowGrid(bool show);
    /// Colours and grid-line policy.
    [[nodiscard]] const render::RenderStyle& style() const noexcept;
    /// Call right after World::resize(): fits and centres the world.
    void worldExtentChanged();
    /// World size that fills the canvas at the current cell size.
    [[nodiscard]] core::Extent cellsThatFit() const noexcept;
    /// Ends any drag and releases the mouse capture.
    void cancelStroke();

private:
    enum class Drag : std::uint8_t
    {
        None,
        Paint,
        Pan
    };

    void onPaint(wxPaintEvent& event);
    void onSize(wxSizeEvent& event);
    void onMouse(wxMouseEvent& event);
    void onWheel(wxMouseEvent& event);
    void onScroll(wxScrollWinEvent& event);
    void onKeyDown(wxKeyEvent& event);
    void onChar(wxKeyEvent& event);
    void onCaptureLost(wxMouseCaptureLostEvent& event);
    void onThemeChanged(wxSysColourChangedEvent& event);

    void beginPaint(core::CellPos cell, core::Cell value);
    void continuePaint(render::PixelPoint devicePoint);
    void endDrag();
    void setHovered(std::optional<core::CellPos> cell);
    /// Scrollbars, hovered cell, Refresh(false), callbacks_.viewChanged.
    void viewportChanged();
    /// viewportChanged() after a zoom or scroll by the user; ends a kept fit.
    void cameraMoved();
    /// Re-reads the device client size (re-fitting if kept); true if it changed.
    bool                            syncCanvasSize();
    void                            syncScrollbars();
    [[nodiscard]] render::PixelSize deviceClientSize() const;
    /// × GetContentScaleFactor()
    [[nodiscard]] render::PixelPoint toDevice(wxPoint logical) const;
    [[nodiscard]] render::PixelPoint canvasCentre() const noexcept;

    const core::World&  world_;
    Callbacks           callbacks_;
    render::Viewport    viewport_;
    render::RenderStyle style_;
    render::Rasterizer  rasterizer_;
    render::PixelBuffer frame_;

    Drag       drag_        = Drag::None;
    int        dragButton_  = wxMOUSE_BTN_NONE;  ///< Only this button's release ends the drag.
    core::Cell strokeValue_ = core::kAlive;
    /// nullopt: the next motion starts a new segment.
    std::optional<core::CellPos> lastStrokeCell_;
    /// Reused buffer for one stroke segment.
    std::vector<core::CellPos> strokeCells_;
    render::PixelPoint         lastPanPoint_;
    /// Device pixels; nullopt while the pointer is elsewhere.
    std::optional<render::PixelPoint> pointer_;
    std::optional<core::CellPos>      hovered_;

    bool   keepFitted_       = false;  ///< Set by fitWorld(), cleared by cameraMoved().
    double wheelZoomNotches_ = 0.0;    ///< Leftover fractions from smooth-scrolling devices.
    double wheelPanX_        = 0.0;
    double wheelPanY_        = 0.0;
};

}  // namespace wxLife::ui

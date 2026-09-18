#include "wxLife/ui/WorldCanvas.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

#include <wx/bitmap.h>
#include <wx/dcclient.h>
#include <wx/image.h>

#include "wxLife/core/Line.h"
#include "wxLife/core/Types.h"
#include "wxLife/core/World.h"
#include "wxLife/core/WorldLimits.h"
#include "wxLife/render/Rasterizer.h"
#include "wxLife/render/Types.h"
#include "wxLife/render/Viewport.h"
#include "wxLife/ui/CommandIds.h"
#include "wxLife/ui/Defaults.h"
#include "wxLife/ui/Theme.h"
#include "wxLife/ui/WxConvert.h"

namespace wxLife::ui
{

// Scrollbars take int positions, and they count device pixels of the whole world.
static_assert(std::int64_t{core::kMaxWorldSide} * render::kMaxCellSize <=
              std::numeric_limits<int>::max());

namespace
{

constexpr long kCanvasStyle = wxHSCROLL | wxVSCROLL | wxALWAYS_SHOW_SB | wxWANTS_CHARS |
                              wxFULL_REPAINT_ON_RESIZE | wxBORDER_NONE;

// A wheel notch scrolls kWheelPanLines "lines". A line is one cell, but at least kMinWheelLinePx
// pixels, or kMinScrollbarLinePx for the scrollbar arrows.
constexpr int kWheelPanLines      = 3;
constexpr int kMinWheelLinePx     = 16;
constexpr int kMinScrollbarLinePx = 32;

constexpr render::Pixel kSmallPanPercent = 10;
constexpr render::Pixel kPagePanPercent  = 90;

[[nodiscard]] render::Pixel percentOf(render::Pixel length, render::Pixel percent)
{
    return length * percent / 100;
}

}  // namespace

WorldCanvas::WorldCanvas(wxWindow* parent, const core::World& world, Callbacks callbacks)
  : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, kCanvasStyle),
    world_(world),
    callbacks_(std::move(callbacks)),
    style_(themeStyle())
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);  // onPaint covers every pixel; no erase step
    SetBackgroundColour(toWx(style_.outside));
    style_.showGrid = defaults::kShowGrid;
    viewport_.setWorldExtent(world_.extent());
    viewport_.setCellSize(defaults::kCellSize, {});

    Bind(wxEVT_PAINT, &WorldCanvas::onPaint, this);
    Bind(wxEVT_SIZE, &WorldCanvas::onSize, this);
    for (const auto& type :
         {wxEVT_LEFT_DOWN, wxEVT_LEFT_DCLICK, wxEVT_LEFT_UP, wxEVT_RIGHT_DOWN, wxEVT_RIGHT_DCLICK,
          wxEVT_RIGHT_UP, wxEVT_MIDDLE_DOWN, wxEVT_MIDDLE_DCLICK, wxEVT_MIDDLE_UP, wxEVT_MOTION,
          wxEVT_LEAVE_WINDOW})
    {
        Bind(type, &WorldCanvas::onMouse, this);
    }
    Bind(wxEVT_MOUSEWHEEL, &WorldCanvas::onWheel, this);
    for (const auto& type :
         {wxEVT_SCROLLWIN_TOP, wxEVT_SCROLLWIN_BOTTOM, wxEVT_SCROLLWIN_LINEUP,
          wxEVT_SCROLLWIN_LINEDOWN, wxEVT_SCROLLWIN_PAGEUP, wxEVT_SCROLLWIN_PAGEDOWN,
          wxEVT_SCROLLWIN_THUMBTRACK, wxEVT_SCROLLWIN_THUMBRELEASE})
    {
        Bind(type, &WorldCanvas::onScroll, this);
    }
    Bind(wxEVT_KEY_DOWN, &WorldCanvas::onKeyDown, this);
    Bind(wxEVT_CHAR, &WorldCanvas::onChar, this);
    Bind(wxEVT_MOUSE_CAPTURE_LOST, &WorldCanvas::onCaptureLost, this);
    Bind(wxEVT_SYS_COLOUR_CHANGED, &WorldCanvas::onThemeChanged, this);
}

int WorldCanvas::cellSize() const noexcept
{
    return viewport_.cellSize();
}

void WorldCanvas::setCellSize(int px)
{
    viewport_.setCellSize(px, canvasCentre());
    cameraMoved();
}

void WorldCanvas::zoomBy(int steps)
{
    viewport_.zoomBy(steps, canvasCentre());
    cameraMoved();
}

void WorldCanvas::fitWorld()
{
    // The fit is kept while the canvas size changes: wx reports provisional sizes before and just
    // after Show(), and under Wayland the display scale can still change after the first frame.
    keepFitted_ = true;
    viewport_.setCanvasSize(deviceClientSize());
    viewport_.fitWorld();
    viewportChanged();
}

void WorldCanvas::centerWorld()
{
    const render::PixelSize content = viewport_.contentSize();
    const render::PixelSize canvas  = viewport_.canvasSize();
    viewport_.scrollTo(
        {.x = (content.width - canvas.width) / 2, .y = (content.height - canvas.height) / 2});
    viewportChanged();
}

bool WorldCanvas::showGrid() const noexcept
{
    return style_.showGrid;
}

void WorldCanvas::setShowGrid(bool show)
{
    style_.showGrid = show;
    Refresh(false);
}

const render::RenderStyle& WorldCanvas::style() const noexcept
{
    return style_;
}

void WorldCanvas::worldExtentChanged()
{
    viewport_.setWorldExtent(world_.extent());
    fitWorld();
}

core::Extent WorldCanvas::cellsThatFit() const noexcept
{
    const render::PixelSize canvas = viewport_.canvasSize();
    const render::Pixel     cell   = viewport_.cellSize();

    const auto cellsAlong = [cell](render::Pixel length) {
        return static_cast<core::Coord>(
            std::clamp<render::Pixel>(length / cell, core::kMinWorldSide, core::kMaxWorldSide));
    };
    return {.width = cellsAlong(canvas.width), .height = cellsAlong(canvas.height)};
}

void WorldCanvas::cancelStroke()
{
    endDrag();
}

void WorldCanvas::onPaint(wxPaintEvent& /*event*/)
{
    wxPaintDC dc(this);
    wxASSERT(viewport_.worldExtent() == world_.extent());
    // The client area can change without a size event, for example with the display scale. Only
    // the viewport changes inside a paint handler; the scrollbars and the listeners follow right
    // after it.
    if (syncCanvasSize())
    {
        CallAfter([this] { viewportChanged(); });
    }

    rasterizer_.render(world_.cells(), viewport_, style_, frame_);
    if (world_.automaton() == core::Automaton::LangtonAnt)
    {
        render::drawAnts(world_.ants(), viewport_, style_, frame_);
    }
    const auto [width, height] = frame_.size();
    if (width <= 0 || height <= 0)
    {
        return;
    }
    // static_data: the image borrows frame_'s bytes instead of copying them.
    const wxImage image(static_cast<int>(width), static_cast<int>(height), frame_.bytes().data(),
                        true);
    // Carrying the scale factor makes wx draw the bitmap at logical size, i.e. 1:1 on the
    // physical screen.
    dc.DrawBitmap(wxBitmap(image, wxBITMAP_SCREEN_DEPTH, GetContentScaleFactor()), 0, 0);
}

void WorldCanvas::onSize(wxSizeEvent& event)
{
    if (syncCanvasSize())
    {
        viewportChanged();
    }
    event.Skip();
}

void WorldCanvas::onMouse(wxMouseEvent& event)
{
    event.Skip();  // lets wx and GTK do their default processing too
    const render::PixelPoint point = toDevice(event.GetPosition());

    if (event.GetEventType() == wxEVT_LEAVE_WINDOW)
    {
        pointer_.reset();
        setHovered(std::nullopt);
    }
    else if (event.GetEventType() == wxEVT_MOTION)
    {
        pointer_ = point;
        setHovered(viewport_.cellAt(point));
        if (drag_ == Drag::Paint)
        {
            continuePaint(point);
        }
        else if (drag_ == Drag::Pan)
        {
            viewport_.panBy(lastPanPoint_.x - point.x, lastPanPoint_.y - point.y);
            lastPanPoint_ = point;
            cameraMoved();
        }
    }
    else if (event.ButtonDown() || event.ButtonDClick())
    {
        // wxGTK sends the second press of a double click only as a DCLICK, so that counts as a
        // press too.
        SetFocus();  // single-key shortcuts need the focus
        if (drag_ != Drag::None)
        {
            return;  // a second button during a drag changes nothing
        }
        const int button = event.GetButton();

        dragButton_ = button;
        if (button == wxMOUSE_BTN_MIDDLE || (button == wxMOUSE_BTN_LEFT && event.ShiftDown()))
        {
            drag_         = Drag::Pan;
            lastPanPoint_ = point;
            CaptureMouse();
        }
        else if (button == wxMOUSE_BTN_LEFT && event.ControlDown())
        {
            // Places an ant instead of drawing, so no stroke starts and the mouse is not captured.
            if (const std::optional<core::CellPos> cell = viewport_.cellAt(point))
            {
                callbacks_.toggleAnt(*cell);
            }
        }
        else if (const std::optional<core::CellPos> cell = viewport_.cellAt(point))
        {
            // Left toggles: pressing a live cell erases, pressing a dead one draws. Right always
            // erases.
            const bool erase = button == wxMOUSE_BTN_RIGHT || world_.at(*cell) == core::kAlive;
            beginPaint(*cell, erase ? core::kDead : core::kAlive);
        }
    }
    else if (event.ButtonUp() && event.GetButton() == dragButton_)
    {
        endDrag();
    }
}

// Never calls Skip(): GTK's scrolled window would scroll the canvas a second time.
void WorldCanvas::onWheel(wxMouseEvent& event)
{
    const render::PixelPoint pointer = toDevice(event.GetPosition());
    // Smooth-scrolling devices send fractions of a notch; the leftovers are kept for the next
    // event.
    const double notches = static_cast<double>(event.GetWheelRotation()) / event.GetWheelDelta();
    const bool   horizontalAxis = event.GetWheelAxis() == wxMOUSE_WHEEL_HORIZONTAL;

    if (event.ControlDown())
    {
        if (horizontalAxis)
        {
            return;
        }
        wheelZoomNotches_ += notches;
        const auto steps = static_cast<int>(wheelZoomNotches_);  // whole notches, toward zero
        if (steps == 0)
        {
            return;
        }
        wheelZoomNotches_ -= steps;
        viewport_.zoomBy(steps, pointer);
    }
    else
    {
        // wxGTK: a positive rotation means up on the vertical axis but right on the horizontal one.
        const int    direction = horizontalAxis ? 1 : -1;
        const double pixels =
            notches * kWheelPanLines * std::max(viewport_.cellSize(), kMinWheelLinePx) * direction;
        const bool horizontal = horizontalAxis || event.ShiftDown();
        double&    pending    = horizontal ? wheelPanX_ : wheelPanY_;

        pending += pixels;
        const auto whole = static_cast<render::Pixel>(pending);
        if (whole == 0)
        {
            return;
        }
        pending -= static_cast<double>(whole);
        if (horizontal)
        {
            viewport_.panBy(whole, 0);
        }
        else
        {
            viewport_.panBy(0, whole);
        }
    }
    pointer_ = pointer;
    cameraMoved();
}

void WorldCanvas::onScroll(wxScrollWinEvent& event)
{
    const bool               horizontal = event.GetOrientation() == wxHORIZONTAL;
    const render::PixelPoint offset     = viewport_.offset();

    const auto along = [horizontal](render::PixelSize size) {
        return horizontal ? size.width : size.height;
    };
    const auto moveBy = [&](render::Pixel delta) {
        if (horizontal)
        {
            viewport_.panBy(delta, 0);
        }
        else
        {
            viewport_.panBy(0, delta);
        }
    };
    const auto moveTo = [&](render::Pixel position) {
        viewport_.scrollTo(horizontal ? render::PixelPoint{.x = position, .y = offset.y}
                                      : render::PixelPoint{.x = offset.x, .y = position});
    };
    const render::Pixel line = std::max(viewport_.cellSize(), kMinScrollbarLinePx);
    const render::Pixel page = percentOf(along(viewport_.canvasSize()), kPagePanPercent);

    // Scroll event types are not constant expressions, so no switch.
    const wxEventType type = event.GetEventType();
    if (type == wxEVT_SCROLLWIN_LINEUP)
    {
        moveBy(-line);
    }
    else if (type == wxEVT_SCROLLWIN_LINEDOWN)
    {
        moveBy(line);
    }
    else if (type == wxEVT_SCROLLWIN_PAGEUP)
    {
        moveBy(-page);
    }
    else if (type == wxEVT_SCROLLWIN_PAGEDOWN)
    {
        moveBy(page);
    }
    else if (type == wxEVT_SCROLLWIN_TOP)
    {
        moveTo(0);
    }
    else if (type == wxEVT_SCROLLWIN_BOTTOM)
    {
        moveTo(along(viewport_.contentSize()) - along(viewport_.canvasSize()));
    }
    else  // THUMBTRACK, THUMBRELEASE
    {
        moveTo(event.GetPosition());
    }
    cameraMoved();
}

void WorldCanvas::onKeyDown(wxKeyEvent& event)
{
    const auto send = [this](CommandId id) { emitCommand(*this, id); };
    // GTK's scrolled window around the canvas binds Ctrl+Home and would take it from the menu's
    // accelerator.
    if (event.GetModifiers() == wxMOD_CONTROL && event.GetKeyCode() == WXK_HOME)
    {
        send(ID_CENTER_VIEW);
        return;
    }
    // Ctrl and Alt combinations belong to the menu accelerators. (Shift does not count as a
    // modifier here.)
    if (event.HasModifiers())
    {
        event.Skip();
        return;
    }
    const render::PixelSize canvas       = viewport_.canvasSize();
    const render::Pixel     arrowPercent = event.ShiftDown() ? kPagePanPercent : kSmallPanPercent;

    const auto pan = [this](render::Pixel dx, render::Pixel dy) {
        viewport_.panBy(dx, dy);
        cameraMoved();
    };

    switch (event.GetKeyCode())
    {
        case WXK_LEFT:
            pan(-percentOf(canvas.width, arrowPercent), 0);
            break;
        case WXK_RIGHT:
            pan(percentOf(canvas.width, arrowPercent), 0);
            break;
        case WXK_UP:
            pan(0, -percentOf(canvas.height, arrowPercent));
            break;
        case WXK_DOWN:
            pan(0, percentOf(canvas.height, arrowPercent));
            break;
        case WXK_PAGEUP:
            pan(0, -percentOf(canvas.height, kPagePanPercent));
            break;
        case WXK_PAGEDOWN:
            pan(0, percentOf(canvas.height, kPagePanPercent));
            break;
        case WXK_ESCAPE:
            cancelStroke();
            break;
        case WXK_SPACE:
            send(ID_RUN_PAUSE);
            break;
        case 'N':
            send(ID_STEP);
            break;
        case WXK_NUMPAD_ADD:
            send(ID_ZOOM_IN);
            break;
        case WXK_NUMPAD_SUBTRACT:
            send(ID_ZOOM_OUT);
            break;
        case 'F':
            send(ID_ZOOM_FIT);
            break;
        case 'C':
        case WXK_HOME:
            send(ID_CENTER_VIEW);
            break;
        case 'G':
            send(ID_TOGGLE_GRID);
            break;
        case 'W':
            send(ID_TOGGLE_WRAP);
            break;
        default:
            event.Skip();  // wx then sends the char event that onChar() handles
            break;
    }
}

// Shortcuts that name a character are matched here: wxGTK reports punctuation keys in key-down
// events as a US layout would. On a German layout ']' is AltGr+9, which onKeyDown() sees as
// Ctrl+Alt+'9'.
void WorldCanvas::onChar(wxKeyEvent& event)
{
    // wx reports AltGr as Ctrl+Alt. Other Ctrl and Alt combinations belong to the menu
    // accelerators.
    const int modifiers = event.GetModifiers() & ~wxMOD_SHIFT;
    if (modifiers != wxMOD_NONE && modifiers != wxMOD_ALTGR)
    {
        event.Skip();
        return;
    }
    const auto send = [this](CommandId id) { emitCommand(*this, id); };
    switch (event.GetUnicodeKey())
    {
        case ']':
            send(ID_FASTER);
            break;
        case '[':
            send(ID_SLOWER);
            break;
        case '+':
        case '=':
            send(ID_ZOOM_IN);
            break;
        case '-':
            send(ID_ZOOM_OUT);
            break;
        default:
            event.Skip();
            break;
    }
}

void WorldCanvas::onCaptureLost(wxMouseCaptureLostEvent& /*event*/)
{
    // wx has already dropped the capture, so endDrag() finds nothing to release.
    endDrag();
}

void WorldCanvas::onThemeChanged(wxSysColourChangedEvent& event)
{
    const bool showGrid = style_.showGrid;

    style_          = themeStyle();
    style_.showGrid = showGrid;
    SetBackgroundColour(toWx(style_.outside));
    Refresh(false);
    event.Skip();
}

void WorldCanvas::beginPaint(core::CellPos cell, core::Cell value)
{
    drag_           = Drag::Paint;
    strokeValue_    = value;
    lastStrokeCell_ = cell;
    CaptureMouse();
    const std::array firstCell{cell};
    callbacks_.paintCells(firstCell, value);
}

void WorldCanvas::continuePaint(render::PixelPoint devicePoint)
{
    const core::CellPos target = viewport_.cellAtClamped(devicePoint);
    if (!lastStrokeCell_)  // the camera moved (see viewportChanged())
    {
        lastStrokeCell_ = target;
        const std::array firstCell{target};
        callbacks_.paintCells(firstCell, strokeValue_);
        return;
    }
    if (target == *lastStrokeCell_)
    {
        return;
    }
    // Painting the whole line from the previous cell leaves no gaps, however fast the pointer
    // moves.
    strokeCells_.clear();
    core::forEachCellOnLine(*lastStrokeCell_, target,
                            [this](core::CellPos c) { strokeCells_.push_back(c); });
    lastStrokeCell_ = target;
    // The line starts with the previous cell, which the last segment already painted.
    callbacks_.paintCells(std::span(strokeCells_).subspan(1), strokeValue_);
}

void WorldCanvas::endDrag()
{
    drag_       = Drag::None;
    dragButton_ = wxMOUSE_BTN_NONE;
    lastStrokeCell_.reset();
    // wx asserts when a window is destroyed while it holds the capture, or when a capture is
    // released twice.
    if (HasCapture())
    {
        ReleaseMouse();
    }
}

void WorldCanvas::setHovered(std::optional<core::CellPos> cell)
{
    if (cell == hovered_)
    {
        return;
    }
    hovered_ = cell;
    callbacks_.hoverChanged(cell);
}

void WorldCanvas::viewportChanged()
{
    syncScrollbars();
    // A zoom or scroll from the keyboard or a scrollbar moves the view under a resting pointer.
    setHovered(pointer_ ? viewport_.cellAt(*pointer_) : std::nullopt);
    // A stroke goes on from the cell now under the pointer. A line from the last cell would cross
    // cells the pointer never touched.
    if (drag_ == Drag::Paint)
    {
        lastStrokeCell_.reset();
    }
    Refresh(false);
    callbacks_.viewChanged();
}

void WorldCanvas::cameraMoved()
{
    keepFitted_ = false;
    viewportChanged();
}

bool WorldCanvas::syncCanvasSize()
{
    const render::PixelSize size = deviceClientSize();
    if (size == viewport_.canvasSize())
    {
        return false;
    }
    viewport_.setCanvasSize(size);
    if (keepFitted_)
    {
        viewport_.fitWorld();
    }
    return true;
}

void WorldCanvas::syncScrollbars()
{
    const auto setBar = [this](int orientation, render::Pixel offset, render::Pixel canvas,
                               render::Pixel content) {
        // A centred world has a negative offset; the scrollbar then rests at 0. The range is at
        // least one page, because GTK shades a smaller range as if it were scrolled past its end.
        SetScrollbar(orientation, static_cast<int>(std::max<render::Pixel>(offset, 0)),
                     static_cast<int>(canvas), static_cast<int>(std::max(content, canvas)));
    };
    // The first SetScrollbar() can still change the client size, so check once more.
    for (int pass = 0; pass < 2; ++pass)
    {
        const render::PixelPoint offset  = viewport_.offset();
        const render::PixelSize  canvas  = viewport_.canvasSize();
        const render::PixelSize  content = viewport_.contentSize();
        setBar(wxHORIZONTAL, offset.x, canvas.width, content.width);
        setBar(wxVERTICAL, offset.y, canvas.height, content.height);
        if (!syncCanvasSize())
        {
            break;
        }
    }
}

render::PixelSize WorldCanvas::deviceClientSize() const
{
    const wxSize size  = GetClientSize();
    const double scale = GetContentScaleFactor();
    return {.width  = static_cast<render::Pixel>(std::llround(size.x * scale)),
            .height = static_cast<render::Pixel>(std::llround(size.y * scale))};
}

render::PixelPoint WorldCanvas::toDevice(wxPoint logical) const
{
    const double scale = GetContentScaleFactor();
    return {.x = static_cast<render::Pixel>(std::llround(logical.x * scale)),
            .y = static_cast<render::Pixel>(std::llround(logical.y * scale))};
}

render::PixelPoint WorldCanvas::canvasCentre() const noexcept
{
    const render::PixelSize canvas = viewport_.canvasSize();
    return {.x = canvas.width / 2, .y = canvas.height / 2};
}

}  // namespace wxLife::ui

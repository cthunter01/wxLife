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
    m_world(world),
    m_callbacks(std::move(callbacks)),
    m_style(themeStyle())
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);  // onPaint covers every pixel; no erase step
    SetBackgroundColour(toWx(m_style.outside));
    m_style.showGrid = defaults::kShowGrid;
    if (m_world.kind() == core::WorldKind::UNBOUNDED)
    {
        m_viewport.setUnbounded();
    }
    else
    {
        m_viewport.setWorldExtent(m_world.extent());
    }
    m_viewport.setCellSize(defaults::kCellSize, {});

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

render::Scale WorldCanvas::scale() const noexcept
{
    return m_viewport.scale();
}

unsigned WorldCanvas::maxShrink() const noexcept
{
    return m_viewport.maxShrink();
}

void WorldCanvas::setScale(render::Scale scale)
{
    m_viewport.setScale(scale, canvasCentre());
    cameraMoved();
}

void WorldCanvas::zoomBy(int steps)
{
    m_viewport.zoomBy(steps, canvasCentre());
    cameraMoved();
}

void WorldCanvas::fitWorld()
{
    if (m_world.kind() == core::WorldKind::FIXED_SIZE)
    {
        const core::Extent extent = m_world.extent();
        showCells({.x0 = 0, .y0 = 0, .x1 = extent.width, .y1 = extent.height});
        return;
    }
    if (const std::optional<core::UniverseRect> bounds = m_world.plane().bounds())
    {
        showCells(*bounds);
        return;
    }
    m_keptFit.reset();
    m_viewport.setCanvasSize(deviceClientSize());
    m_viewport.centerOn({});
    viewportChanged();
}

void WorldCanvas::showCells(core::UniverseRect cells)
{
    // The fit is kept while the canvas size changes: wx reports provisional sizes before and just
    // after Show(), and under Wayland the display scale can still change after the first frame.
    m_keptFit = cells;
    m_viewport.setCanvasSize(deviceClientSize());
    m_viewport.fitCells(cells);
    viewportChanged();
}

void WorldCanvas::centerWorld()
{
    if (m_world.kind() == core::WorldKind::UNBOUNDED)
    {
        const std::optional<core::UniverseRect> bounds = m_world.plane().bounds();
        m_viewport.centerOn(
            bounds ? core::UniversePos{.x = bounds->x0 + ((bounds->x1 - bounds->x0) / 2),
                                       .y = bounds->y0 + ((bounds->y1 - bounds->y0) / 2)}
                   : core::UniversePos{});
        viewportChanged();
        return;
    }
    const render::PixelSize content = m_viewport.contentSize();
    const render::PixelSize canvas  = m_viewport.canvasSize();
    m_viewport.scrollTo(
        {.x = (content.width - canvas.width) / 2, .y = (content.height - canvas.height) / 2});
    viewportChanged();
}

bool WorldCanvas::showGrid() const noexcept
{
    return m_style.showGrid;
}

void WorldCanvas::setShowGrid(bool show)
{
    m_style.showGrid = show;
    Refresh(false);
}

const render::RenderStyle& WorldCanvas::style() const noexcept
{
    return m_style;
}

void WorldCanvas::worldExtentChanged()
{
    if (m_world.kind() == core::WorldKind::UNBOUNDED)
    {
        m_viewport.setUnbounded();
    }
    else
    {
        m_viewport.setWorldExtent(m_world.extent());
    }
    fitWorld();
}

core::UniverseRect WorldCanvas::visibleCells() const noexcept
{
    return m_viewport.visibleCells();
}

core::Extent WorldCanvas::cellsThatFit() const noexcept
{
    const render::PixelSize canvas = m_viewport.canvasSize();
    const render::Scale     scale  = m_viewport.scale();

    const auto cellsAlong = [scale](render::Pixel length) {
        // Both factors are capped at the largest side first, so the product cannot overflow.
        const render::Pixel cells =
            scale.zoomedOut()
                ? std::min<render::Pixel>(length, core::kMaxWorldSide) *
                      std::min<render::Pixel>(scale.cellsPerPixel(), core::kMaxWorldSide)
                : length / scale.cellSize;
        return static_cast<core::Coord>(
            std::clamp<render::Pixel>(cells, core::kMinWorldSide, core::kMaxWorldSide));
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
    wxASSERT(m_viewport.unbounded() == (m_world.kind() == core::WorldKind::UNBOUNDED));
    wxASSERT(m_viewport.worldExtent() == m_world.extent());
    // The client area can change without a size event, for example with the display scale. Only
    // the viewport changes inside a paint handler; the scrollbars and the listeners follow right
    // after it.
    if (syncCanvasSize())
    {
        CallAfter([this] { viewportChanged(); });
    }

    if (m_world.kind() == core::WorldKind::UNBOUNDED)
    {
        // Safe while a step runs on the worker thread: it reads the plane as it was before.
        m_rasterizer.render(m_world.plane(), m_viewport, m_style, m_frame);
    }
    else
    {
        m_rasterizer.render(m_world.cells(), m_viewport, m_style, m_frame);
    }
    if (m_world.automaton() == core::Automaton::LANGTON_ANT)
    {
        render::drawAnts(m_world.ants(), m_viewport, m_style, m_frame);
    }
    const auto [width, height] = m_frame.size();
    if (width <= 0 || height <= 0)
    {
        return;
    }
    // static_data: the image borrows m_frame's bytes instead of copying them.
    const wxImage image(static_cast<int>(width), static_cast<int>(height), m_frame.bytes().data(),
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
        m_pointer.reset();
        setHovered(std::nullopt);
    }
    else if (event.GetEventType() == wxEVT_MOTION)
    {
        m_pointer = point;
        setHovered(m_viewport.cellAt(point));
        if (m_drag == Drag::PAINT)
        {
            continuePaint(point);
        }
        else if (m_drag == Drag::PAN)
        {
            m_viewport.panBy(m_lastPanPoint.x - point.x, m_lastPanPoint.y - point.y);
            m_lastPanPoint = point;
            cameraMoved();
        }
    }
    else if (event.ButtonDown() || event.ButtonDClick())
    {
        // wxGTK sends the second press of a double click only as a DCLICK, so that counts as a
        // press too.
        SetFocus();  // single-key shortcuts need the focus
        if (m_drag != Drag::NONE)
        {
            return;  // a second button during a drag changes nothing
        }
        const int button = event.GetButton();

        m_dragButton = button;
        // Below 1 px a pixel is a block of cells, so no button draws or places an ant there:
        // every drag pans.
        if (button == wxMOUSE_BTN_MIDDLE || (button == wxMOUSE_BTN_LEFT && event.ShiftDown()) ||
            m_viewport.scale().zoomedOut())
        {
            m_drag         = Drag::PAN;
            m_lastPanPoint = point;
            CaptureMouse();
        }
        else if (button == wxMOUSE_BTN_LEFT && event.ControlDown())
        {
            // Places an ant instead of drawing, so no stroke starts and the mouse is not captured.
            if (const std::optional<core::UniversePos> cell = m_viewport.cellAt(point))
            {
                m_callbacks.toggleAnt(*cell);
            }
        }
        else if (const std::optional<core::UniversePos> cell = m_viewport.cellAt(point))
        {
            // Left toggles: pressing a live cell erases, pressing a dead one draws. Right always
            // erases.
            const bool erase = button == wxMOUSE_BTN_RIGHT || m_world.cellAt(*cell) == core::kAlive;
            beginPaint(*cell, erase ? core::kDead : core::kAlive);
        }
    }
    else if (event.ButtonUp() && event.GetButton() == m_dragButton)
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
        m_wheelZoomNotches += notches;
        const auto steps = static_cast<int>(m_wheelZoomNotches);  // whole notches, toward zero
        if (steps == 0)
        {
            return;
        }
        m_wheelZoomNotches -= steps;
        m_viewport.zoomBy(steps, pointer);
    }
    else
    {
        // wxGTK: a positive rotation means up on the vertical axis but right on the horizontal one.
        const int    direction = horizontalAxis ? 1 : -1;
        const double pixels    = notches * kWheelPanLines *
                                 std::max(m_viewport.scale().cellSize, kMinWheelLinePx) * direction;
        const bool   horizontal = horizontalAxis || event.ShiftDown();
        double&      pending    = horizontal ? m_wheelPanX : m_wheelPanY;

        pending += pixels;
        const auto whole = static_cast<render::Pixel>(pending);
        if (whole == 0)
        {
            return;
        }
        pending -= static_cast<double>(whole);
        if (horizontal)
        {
            m_viewport.panBy(whole, 0);
        }
        else
        {
            m_viewport.panBy(0, whole);
        }
    }
    m_pointer = pointer;
    cameraMoved();
}

void WorldCanvas::onScroll(wxScrollWinEvent& event)
{
    if (m_viewport.unbounded())
    {
        return;  // a plane has no ends to scroll between; the bars stay still
    }
    const bool               horizontal = event.GetOrientation() == wxHORIZONTAL;
    const render::PixelPoint offset     = m_viewport.offset();

    const auto along = [horizontal](render::PixelSize size) {
        return horizontal ? size.width : size.height;
    };
    const auto moveBy = [&](render::Pixel delta) {
        if (horizontal)
        {
            m_viewport.panBy(delta, 0);
        }
        else
        {
            m_viewport.panBy(0, delta);
        }
    };
    const auto moveTo = [&](render::Pixel position) {
        m_viewport.scrollTo(horizontal ? render::PixelPoint{.x = position, .y = offset.y}
                                       : render::PixelPoint{.x = offset.x, .y = position});
    };
    const render::Pixel line = std::max(m_viewport.scale().cellSize, kMinScrollbarLinePx);
    const render::Pixel page = percentOf(along(m_viewport.canvasSize()), kPagePanPercent);

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
        moveTo(along(m_viewport.contentSize()) - along(m_viewport.canvasSize()));
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
    const render::PixelSize canvas       = m_viewport.canvasSize();
    const render::Pixel     arrowPercent = event.ShiftDown() ? kPagePanPercent : kSmallPanPercent;

    const auto pan = [this](render::Pixel dx, render::Pixel dy) {
        m_viewport.panBy(dx, dy);
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
        case '}':
            send(ID_LARGER_STEP);
            break;
        case '{':
            send(ID_SMALLER_STEP);
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
    const bool showGrid = m_style.showGrid;

    m_style          = themeStyle();
    m_style.showGrid = showGrid;
    SetBackgroundColour(toWx(m_style.outside));
    Refresh(false);
    event.Skip();
}

void WorldCanvas::beginPaint(core::UniversePos cell, core::Cell value)
{
    m_drag           = Drag::PAINT;
    m_strokeValue    = value;
    m_lastStrokeCell = cell;
    CaptureMouse();
    const std::array firstCell{cell};
    m_callbacks.paintCells(firstCell, value);
}

void WorldCanvas::continuePaint(render::PixelPoint devicePoint)
{
    if (m_viewport.scale().zoomedOut())
    {
        return;  // zoomed below 1 px during the stroke: it waits until the cells are pixels again
    }
    const core::UniversePos target = m_viewport.cellAtClamped(devicePoint);
    if (!m_lastStrokeCell)  // the camera moved (see viewportChanged())
    {
        m_lastStrokeCell = target;
        const std::array firstCell{target};
        m_callbacks.paintCells(firstCell, m_strokeValue);
        return;
    }
    if (target == *m_lastStrokeCell)
    {
        return;
    }
    // Painting the whole line from the previous cell leaves no gaps, however fast the pointer
    // moves.
    m_strokeCells.clear();
    core::forEachCellOnLine(*m_lastStrokeCell, target,
                            [this](core::UniversePos c) { m_strokeCells.push_back(c); });
    m_lastStrokeCell = target;
    // The line starts with the previous cell, which the last segment already painted.
    m_callbacks.paintCells(std::span(m_strokeCells).subspan(1), m_strokeValue);
}

void WorldCanvas::endDrag()
{
    m_drag       = Drag::NONE;
    m_dragButton = wxMOUSE_BTN_NONE;
    m_lastStrokeCell.reset();
    // wx asserts when a window is destroyed while it holds the capture, or when a capture is
    // released twice.
    if (HasCapture())
    {
        ReleaseMouse();
    }
}

void WorldCanvas::setHovered(std::optional<core::UniversePos> cell)
{
    if (cell == m_hovered)
    {
        return;
    }
    m_hovered = cell;
    m_callbacks.hoverChanged(cell);
}

void WorldCanvas::viewportChanged()
{
    syncScrollbars();
    // A zoom or scroll from the keyboard or a scrollbar moves the view under a resting pointer.
    setHovered(m_pointer ? m_viewport.cellAt(*m_pointer) : std::nullopt);
    // A stroke goes on from the cell now under the pointer. A line from the last cell would cross
    // cells the pointer never touched.
    if (m_drag == Drag::PAINT)
    {
        m_lastStrokeCell.reset();
    }
    Refresh(false);
    m_callbacks.viewChanged();
}

void WorldCanvas::cameraMoved()
{
    m_keptFit.reset();
    viewportChanged();
}

bool WorldCanvas::syncCanvasSize()
{
    const render::PixelSize size = deviceClientSize();
    if (size == m_viewport.canvasSize())
    {
        return false;
    }
    m_viewport.setCanvasSize(size);
    if (m_keptFit)
    {
        m_viewport.fitCells(*m_keptFit);
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
        const render::PixelSize canvas = m_viewport.canvasSize();
        // A plane has no ends: the bars show a range of one page, so there is nothing to scroll.
        const render::PixelPoint offset =
            m_viewport.unbounded() ? render::PixelPoint{} : m_viewport.offset();
        const render::PixelSize content =
            m_viewport.unbounded() ? canvas : m_viewport.contentSize();
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
    const render::PixelSize canvas = m_viewport.canvasSize();
    return {.x = canvas.width / 2, .y = canvas.height / 2};
}

}  // namespace wxLife::ui

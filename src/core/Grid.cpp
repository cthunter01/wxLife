#include "wxLife/core/Grid.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <span>

#include "wxLife/core/Types.h"

namespace wxLife::core
{

Grid::Grid() : Grid(Extent{}) { }

Grid::Grid(Extent extent)
  : m_extent(extent),
    m_stride(static_cast<std::size_t>(extent.width) + 2),
    m_cells(m_stride * (static_cast<std::size_t>(extent.height) + 2), kDead)
{
    assert(extent.width >= 0 && extent.height >= 0);
}

Extent Grid::extent() const noexcept
{
    return m_extent;
}

std::size_t Grid::paddedIndex(Coord x, Coord y) const noexcept
{
    return (static_cast<std::size_t>(y + 1) * m_stride) + static_cast<std::size_t>(x + 1);
}

Cell Grid::at(CellPos p) const noexcept
{
    assert(m_extent.contains(p));
    return m_cells[paddedIndex(p.x, p.y)];
}

void Grid::set(CellPos p, Cell value) noexcept
{
    assert(m_extent.contains(p) && (value == kDead || value == kAlive));
    m_cells[paddedIndex(p.x, p.y)] = value;
}

std::span<const Cell> Grid::row(Coord y) const noexcept
{
    return std::span(m_cells).subspan(paddedIndex(0, y), static_cast<std::size_t>(m_extent.width));
}

std::span<Cell> Grid::row(Coord y) noexcept
{
    return std::span(m_cells).subspan(paddedIndex(0, y), static_cast<std::size_t>(m_extent.width));
}

std::span<const Cell> Grid::paddedRow(Coord y) const noexcept
{
    return std::span(m_cells).subspan(paddedIndex(-1, y), m_stride);
}

void Grid::updateBorder(Topology topology) noexcept
{
    const auto width  = static_cast<std::size_t>(m_extent.width);
    const auto height = static_cast<std::size_t>(m_extent.height);
    // Padded row 0 is row -1 and padded row height + 1 is row height.
    const auto paddedRowAt = [this](std::size_t index) {
        return std::span(m_cells).subspan(index * m_stride, m_stride);
    };

    // An empty grid has no opposite edge to copy from.
    if (m_extent.cellCount() == 0)
    {
        topology = Topology::BOUNDED;
    }

    switch (topology)
    {
        case Topology::BOUNDED:
            std::ranges::fill(paddedRowAt(0), kDead);
            std::ranges::fill(paddedRowAt(height + 1), kDead);
            for (std::size_t y = 1; y <= height; ++y)
            {
                const std::span<Cell> padded = paddedRowAt(y);

                padded.front() = kDead;
                padded.back()  = kDead;
            }
            break;
        case Topology::TORUS:
            // Wrap the columns of every interior row first; the whole-row copies below then carry
            // those ghost cells along, which is what makes the four corners correct.
            for (std::size_t y = 1; y <= height; ++y)
            {
                const std::span<Cell> padded = paddedRowAt(y);

                padded[0]         = padded[width];  // column -1 = column width - 1
                padded[width + 1] = padded[1];      // column width = column 0
            }
            // Row -1 = row height - 1, and row height = row 0.
            std::ranges::copy(paddedRowAt(height), paddedRowAt(0).begin());
            std::ranges::copy(paddedRowAt(1), paddedRowAt(height + 1).begin());
            break;
    }
}

void Grid::clear() noexcept
{
    std::ranges::fill(m_cells, kDead);
}

CellCount Grid::countAlive() const noexcept
{
    CellCount alive = 0;
    for (Coord y = 0; y < m_extent.height; ++y)
    {
        alive += std::ranges::count(row(y), kAlive);
    }
    return alive;
}

bool operator==(const Grid& a, const Grid& b) noexcept
{
    if (a.m_extent != b.m_extent)
    {
        return false;
    }
    for (Coord y = 0; y < a.m_extent.height; ++y)
    {
        if (!std::ranges::equal(a.row(y), b.row(y)))
        {
            return false;
        }
    }
    return true;
}

}  // namespace wxLife::core

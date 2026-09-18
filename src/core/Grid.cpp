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
  : extent_(extent),
    stride_(static_cast<std::size_t>(extent.width) + 2),
    cells_(stride_ * (static_cast<std::size_t>(extent.height) + 2), kDead)
{
    assert(extent.width >= 0 && extent.height >= 0);
}

Extent Grid::extent() const noexcept
{
    return extent_;
}

std::size_t Grid::paddedIndex(Coord x, Coord y) const noexcept
{
    return (static_cast<std::size_t>(y + 1) * stride_) + static_cast<std::size_t>(x + 1);
}

Cell Grid::at(CellPos p) const noexcept
{
    assert(extent_.contains(p));
    return cells_[paddedIndex(p.x, p.y)];
}

void Grid::set(CellPos p, Cell value) noexcept
{
    assert(extent_.contains(p) && (value == kDead || value == kAlive));
    cells_[paddedIndex(p.x, p.y)] = value;
}

std::span<const Cell> Grid::row(Coord y) const noexcept
{
    return std::span(cells_).subspan(paddedIndex(0, y), static_cast<std::size_t>(extent_.width));
}

std::span<Cell> Grid::row(Coord y) noexcept
{
    return std::span(cells_).subspan(paddedIndex(0, y), static_cast<std::size_t>(extent_.width));
}

std::span<const Cell> Grid::paddedRow(Coord y) const noexcept
{
    return std::span(cells_).subspan(paddedIndex(-1, y), stride_);
}

void Grid::updateBorder(Topology topology) noexcept
{
    const auto width  = static_cast<std::size_t>(extent_.width);
    const auto height = static_cast<std::size_t>(extent_.height);
    // Padded row 0 is row -1 and padded row height + 1 is row height.
    const auto paddedRowAt = [this](std::size_t index) {
        return std::span(cells_).subspan(index * stride_, stride_);
    };

    // An empty grid has no opposite edge to copy from.
    if (extent_.cellCount() == 0)
    {
        topology = Topology::Bounded;
    }

    switch (topology)
    {
        case Topology::Bounded:
            std::ranges::fill(paddedRowAt(0), kDead);
            std::ranges::fill(paddedRowAt(height + 1), kDead);
            for (std::size_t y = 1; y <= height; ++y)
            {
                const std::span<Cell> padded = paddedRowAt(y);

                padded.front() = kDead;
                padded.back()  = kDead;
            }
            break;
        case Topology::Torus:
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
    std::ranges::fill(cells_, kDead);
}

CellCount Grid::countAlive() const noexcept
{
    CellCount alive = 0;
    for (Coord y = 0; y < extent_.height; ++y)
    {
        alive += std::ranges::count(row(y), kAlive);
    }
    return alive;
}

bool operator==(const Grid& a, const Grid& b) noexcept
{
    if (a.extent_ != b.extent_)
    {
        return false;
    }
    for (Coord y = 0; y < a.extent_.height; ++y)
    {
        if (!std::ranges::equal(a.row(y), b.row(y)))
        {
            return false;
        }
    }
    return true;
}

}  // namespace wxLife::core

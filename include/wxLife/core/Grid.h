#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "wxLife/core/Types.h"

namespace wxLife::core
{

/// Dense cell storage padded by one ghost cell on every side, so neighbour sums need no edge
/// checks. Interior cell (x, y) is stored at padded position (x + 1, y + 1). A moved-from Grid may
/// only be assigned to or destroyed.
class Grid
{
public:
    Grid();                        ///< 0×0: only the four ghost cells. @throws std::bad_alloc
    explicit Grid(Extent extent);  ///< All dead. @throws std::bad_alloc

    [[nodiscard]] Extent extent() const noexcept;
    /// @pre extent().contains(p)
    [[nodiscard]] Cell at(CellPos p) const noexcept;
    /// @pre extent().contains(p), value is 0 or 1
    void set(CellPos p, Cell value) noexcept;

    /// Interior cells of row y.
    [[nodiscard]] std::span<const Cell> row(Coord y) const noexcept;
    [[nodiscard]] std::span<Cell>       row(Coord y) noexcept;
    /// Row y with both ghost cells (width + 2 long). y may also be -1 or height.
    [[nodiscard]] std::span<const Cell> paddedRow(Coord y) const noexcept;

    /// Rewrites the ghost border: zeros (Bounded) or copies of the opposite edges (Torus).
    /// O(width + height).
    void updateBorder(Topology topology) noexcept;
    /// Every cell, border included, becomes dead.
    void                    clear() noexcept;
    [[nodiscard]] CellCount countAlive() const noexcept;

    /// Compares extent and interior cells; ghost cells are ignored.
    friend bool operator==(const Grid& a, const Grid& b) noexcept;

private:
    /// (y + 1) * stride + (x + 1), computed in size_t. x and y may be -1.
    [[nodiscard]] std::size_t paddedIndex(Coord x, Coord y) const noexcept;

    Extent            extent_{};
    std::size_t       stride_ = 0;  ///< width + 2
    std::vector<Cell> cells_;       ///< (width + 2) × (height + 2), row-major
};

}  // namespace wxLife::core

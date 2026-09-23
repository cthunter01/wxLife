/// @file
/// Small pictures of patterns, for the demo list.
#pragma once

#include <span>

#include "wxLife/core/Ant.h"
#include "wxLife/core/HashLife.h"
#include "wxLife/core/Types.h"
#include "wxLife/render/PixelBuffer.h"
#include "wxLife/render/RenderStyle.h"
#include "wxLife/render/Types.h"

namespace wxLife::render
{

/// The largest cell a thumbnail draws, in pixels, so a tiny pattern does not become a few blobs.
inline constexpr int kMaxThumbnailCellSize = 12;
/// The side of an ant's square in a thumbnail, in pixels, at least; smaller would vanish.
inline constexpr int kMinThumbnailAntSize = 3;

/// Draws the live `cells` of an `extent`-sized area, and the `ants` on it, into `out`, as large as
/// fits in `maxSize`:
/// - When the area fits at 1 pixel per cell, every cell becomes a square of the same whole number
///   of pixels, at most kMaxThumbnailCellSize.
/// - Otherwise it is scaled down to fit, keeping its proportions: each pixel covers a rectangle of
///   cells, and is alive when any of them is.
///
/// There are no grid lines. An ant is a square in style.ant, at least kMinThumbnailAntSize wide,
/// centred on its cell. `out` is resized to the picture: empty when the area or maxSize is.
/// @pre every cell and ant lies inside `extent`
void drawThumbnail(std::span<const core::CellPos> cells, std::span<const core::Ant> ants,
                   core::Extent extent, PixelSize maxSize, const RenderStyle& style,
                   PixelBuffer& out);

/// The same for the cells of `area` on an unbounded plane, which may be far larger than any
/// list of cells could be: the plane gives its blocks of 2^k × 2^k cells, for the smallest k whose
/// blocks fit `maxSize`, and each block is drawn as a cell above.
void drawThumbnail(const core::HashLife& plane, core::UniverseRect area, PixelSize maxSize,
                   const RenderStyle& style, PixelBuffer& out);

}  // namespace wxLife::render

#include "wxLife/render/Thumbnail.h"

#include <array>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "wxLife/core/Ant.h"
#include "wxLife/core/Types.h"
#include "wxLife/render/PixelBuffer.h"
#include "wxLife/render/RenderStyle.h"
#include "wxLife/render/Types.h"

namespace wxLife::render
{
namespace
{

using core::Ant;
using core::CellPos;

constexpr RenderStyle kStyle = darkStyle();

// 'O' alive, '.' dead, 'A' ant, '?' anything else.
char symbol(Rgb colour)
{
    if (colour == kStyle.alive)
    {
        return 'O';
    }
    if (colour == kStyle.dead)
    {
        return '.';
    }
    return colour == kStyle.ant ? 'A' : '?';
}

// The picture as text, one line per pixel row.
std::string ascii(const PixelBuffer& picture)
{
    std::string text;
    for (Pixel y = 0; y < picture.size().height; ++y)
    {
        for (Pixel x = 0; x < picture.size().width; ++x)
        {
            text += symbol(picture.at(x, y));
        }
        text += '\n';
    }
    return text;
}

constexpr std::array kGlider{CellPos{.x = 1, .y = 0}, CellPos{.x = 2, .y = 1},
                             CellPos{.x = 0, .y = 2}, CellPos{.x = 1, .y = 2},
                             CellPos{.x = 2, .y = 2}};

TEST(ThumbnailTest, ASmallPatternGetsWholePixelsPerCell)
{
    PixelBuffer picture;
    // 6 × 7 px for 3 × 3 cells: 2 px per cell, the most that fits across.
    drawThumbnail(kGlider, {}, {.width = 3, .height = 3}, {.width = 6, .height = 7}, kStyle,
                  picture);
    EXPECT_EQ(ascii(picture),
              "..OO..\n"
              "..OO..\n"
              "....OO\n"
              "....OO\n"
              "OOOOOO\n"
              "OOOOOO\n");

    // Never more than kMaxThumbnailCellSize, however much room there is.
    drawThumbnail(kGlider, {}, {.width = 3, .height = 3}, {.width = 1000, .height = 1000}, kStyle,
                  picture);
    constexpr Pixel kSide = Pixel{3} * kMaxThumbnailCellSize;
    EXPECT_EQ(picture.size(), (PixelSize{kSide, kSide}));
}

TEST(ThumbnailTest, ALargePatternIsScaledDownToFit)
{
    // 10 × 4 cells in at most 5 × 5 px: the width decides, so 5 × 2 px, each covering 2 × 2 cells.
    // A pixel is alive when any of its cells is.
    PixelBuffer                picture;
    const std::vector<CellPos> cells{{.x = 0, .y = 0}, {.x = 3, .y = 1}, {.x = 9, .y = 3}};
    drawThumbnail(cells, {}, {.width = 10, .height = 4}, {.width = 5, .height = 5}, kStyle,
                  picture);
    EXPECT_EQ(ascii(picture),
              "OO...\n"
              "....O\n");

    // The proportions are kept, however unevenly the sides divide: 11 × 7 becomes 5 × 3.
    drawThumbnail(std::vector<CellPos>{{.x = 10, .y = 6}}, {}, {.width = 11, .height = 7},
                  {.width = 5, .height = 5}, kStyle, picture);
    EXPECT_EQ(ascii(picture),
              ".....\n"
              ".....\n"
              "....O\n");
    // Here the height decides: 7 × 11 becomes 3 × 5.
    drawThumbnail(std::vector<CellPos>{{.x = 6, .y = 10}}, {}, {.width = 7, .height = 11},
                  {.width = 5, .height = 5}, kStyle, picture);
    EXPECT_EQ(picture.size(), (PixelSize{3, 5}));
    EXPECT_EQ(picture.at(2, 4), kStyle.alive);
    // Even a sliver keeps a row of pixels.
    drawThumbnail(std::vector<CellPos>{{.x = 99, .y = 0}}, {}, {.width = 100, .height = 1},
                  {.width = 10, .height = 10}, kStyle, picture);
    EXPECT_EQ(ascii(picture), ".........O\n");
}

TEST(ThumbnailTest, AntsAreSquaresLargeEnoughToSee)
{
    PixelBuffer            picture;
    const std::vector<Ant> ants{{.position = {.x = 3, .y = 2}}, {.position = {.x = 0, .y = 0}}};
    // 1 px per cell, so an ant is a 3 × 3 square around its cell, clipped at the edge.
    drawThumbnail(std::vector<CellPos>{{.x = 6, .y = 4}}, ants, {.width = 7, .height = 5},
                  {.width = 7, .height = 5}, kStyle, picture);
    EXPECT_EQ(ascii(picture),
              "AA.....\n"
              "AAAAA..\n"
              "..AAA..\n"
              "..AAA..\n"
              "......O\n");
}

TEST(ThumbnailTest, NothingToDrawGivesAnEmptyPicture)
{
    PixelBuffer picture;
    drawThumbnail({}, {}, {.width = 0, .height = 5}, {.width = 10, .height = 10}, kStyle, picture);
    EXPECT_EQ(picture.size(), (PixelSize{0, 0}));
    drawThumbnail(kGlider, {}, {.width = 3, .height = 3}, {.width = 0, .height = 10}, kStyle,
                  picture);
    EXPECT_EQ(picture.size(), (PixelSize{0, 0}));
}

}  // namespace
}  // namespace wxLife::render

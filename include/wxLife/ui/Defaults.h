#pragma once

#include <chrono>

#include "wxLife/core/Speed.h"
#include "wxLife/core/Types.h"
#include "wxLife/render/Types.h"

namespace wxLife::ui::defaults
{

inline constexpr core::Extent   kWorldExtent{.width = 512, .height = 512};
inline constexpr core::Topology kTopology = core::Topology::Torus;
inline constexpr core::Speed    kSpeed{.gensPerSecond = 30};
inline constexpr int            kCellSize             = 4;  ///< Used until the first fit.
inline constexpr bool           kShowGrid             = true;
inline constexpr int            kRandomDensityPercent = 25;
/// Ants the panel starts with; the world seeds one when the ant automaton is chosen.
inline constexpr int kAntCount = 1;

inline constexpr int kControlPanelWidthDip = 260;
inline constexpr int kFrameWidthDip        = 1280;
inline constexpr int kFrameHeightDip       = 860;
inline constexpr int kMinFrameWidthDip     = 800;
inline constexpr int kMinFrameHeightDip    = 520;

/// The status bar updates at most this often while the simulation runs.
inline constexpr std::chrono::milliseconds kStatusRefresh{100};

/// Error lines on dark themes.
inline constexpr render::Rgb kErrorTextOnDark{.r = 0xFF, .g = 0x8A, .b = 0x80};
/// Error lines on light themes.
inline constexpr render::Rgb kErrorTextOnLight{.r = 0xC6, .g = 0x28, .b = 0x28};

}  // namespace wxLife::ui::defaults

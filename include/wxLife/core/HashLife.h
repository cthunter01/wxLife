/// @file
/// HashLife: Bill Gosper's algorithm for Life on an unbounded plane. It stores the plane as a
/// quadtree whose identical parts are shared, and it remembers how each part evolves, so regular
/// patterns can jump 2^k generations at once.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "wxLife/core/Rule.h"
#include "wxLife/core/Types.h"

namespace wxLife::core
{

/// Why HashLife::step() could not advance.
enum class HashLifeError : std::uint8_t
{
    OUT_OF_MEMORY,  ///< The step needs more nodes than the memory budget allows.
    UNIVERSE_EDGE,  ///< The pattern would leave the part of the plane a step can reach.
    CANCELLED,      ///< HashLifeStepOptions::cancel was set during the step.
};

/// Message for the user.
[[nodiscard]] constexpr std::string_view describe(HashLifeError e) noexcept
{
    switch (e)
    {
        case HashLifeError::OUT_OF_MEMORY:
            return "The step needs more memory than the budget allows. A smaller step may fit.";
        case HashLifeError::UNIVERSE_EDGE:
            return "The pattern has reached the edge of the universe, 2^59 cells from its centre.";
        case HashLifeError::CANCELLED:
            return "The step was interrupted.";
    }
    std::unreachable();
}

/// How HashLife::step() may run; the options matter when it runs on a thread of its own.
struct HashLifeStepOptions
{
    /// Checked while the step runs: once it is true, the step stops and reports CANCELLED.
    const std::atomic<bool>* cancel = nullptr;
    /// Whether the step may collect garbage when memory runs short. A step on a worker thread must
    /// not, since readers on other threads would see the nodes move: it reports OUT_OF_MEMORY
    /// instead, and the owner collects between steps.
    bool collectGarbage = true;
};

/// A two-state B/S rule running on an unbounded plane with HashLife.
///
/// The plane is a quadtree. A node of level k is a 2^k × 2^k square: four children of level k - 1,
/// or a single cell at level 0. Nodes are hash-consed, so every distinct square exists once, and a
/// node never changes. Each node remembers its result: the centre 2^(k-1) square after 2^(k-2)
/// generations (or fewer, for small steps), computed from nine overlapping results one level down.
/// Periodic and sparse patterns therefore cost almost nothing to advance far.
///
/// Nodes live in fixed-size chunks and keep their ids until collectGarbage(), which is the only
/// thing that moves them. The memory budget caps the number of nodes: a step that would need more
/// is undone, and the garbage is collected, before step() reports OUT_OF_MEMORY.
///
/// @note One thread at a time may change the plane: step(), setCells(), setRule(), clear() and
///       the garbage collection. While a step runs with collectGarbage off, other threads may
///       read: population(), generation(), at(), bounds() and forEachBlock() see the plane as it
///       was before the step, or after it once it has finished. A step writes only nodes nobody
///       has seen yet and the remembered results, which readers never look at, and it publishes
///       its new root atomically.
class HashLife
{
public:
    /// The largest level of a root: its square spans [-2^61, 2^61) on each axis.
    static constexpr unsigned kMaxLevel = 62;
    /// Cells can be set anywhere in [-kRadius, kRadius) on each axis.
    static constexpr UniverseCoord kRadius = UniverseCoord{1} << (kMaxLevel - 1);
    static_assert(kRadius == kUniverseRadius);
    /// The largest step: 2^59 generations. A step also needs the pattern within
    /// [-2^(kMaxLevel-3), 2^(kMaxLevel-3)), so the result stays inside the largest root.
    static constexpr unsigned kMaxStepExponent = kMaxLevel - 3;

    /// @pre supports(rule)
    /// @param memoryBudgetBytes Nodes and their hash table stay within this, give or take one
    ///                          chunk of nodes.
    explicit HashLife(const Rule& rule, std::uint64_t memoryBudgetBytes);

    /// Whether HashLife can run `rule`: any rule without B0, which would fill the unbounded plane
    /// in a single generation.
    [[nodiscard]] static constexpr bool supports(const Rule& rule) noexcept
    {
        return (rule.birthMask() & 1U) == 0;
    }

    [[nodiscard]] const Rule& rule() const noexcept;
    /// Keeps the cells and the generation; forgets every remembered result. @pre supports(rule)
    void setRule(const Rule& rule);

    /// Counts modulo 2^64.
    [[nodiscard]] std::uint64_t generation() const noexcept;
    [[nodiscard]] std::uint64_t population() const noexcept;
    /// Dead outside [-kRadius, kRadius).
    [[nodiscard]] Cell at(UniversePos p) const noexcept;
    /// The smallest rectangle that holds every live cell; nullopt when there is none.
    [[nodiscard]] std::optional<UniverseRect> bounds() const;

    /// Sets every listed cell, moved by `offset`, to `value`; positions outside
    /// [-kRadius, kRadius) are ignored. The generation is unchanged.
    /// @pre value is kDead or kAlive
    /// @return the number of cells that changed.
    /// @throws std::bad_alloc when the memory budget is exhausted; the plane is then unchanged.
    CellCount setCells(std::span<const UniversePos> cells, Cell value, UniversePos offset = {});
    /// The same for cells relative to a pattern or a stroke.
    CellCount setCells(std::span<const CellPos> cells, Cell value, UniversePos offset = {});
    /// Every cell dead, generation 0, and all memory but the empty plane released.
    void clear();

    /// Advances 2^exponent generations. On failure nothing changes; a cancelled step keeps the
    /// results it has already worked out, so the next one is quicker.
    /// @pre exponent <= kMaxStepExponent
    std::expected<void, HashLifeError> step(unsigned                   exponent,
                                            const HashLifeStepOptions& options = {});

    /// Calls `visit` with the top-left corner of every 2^level × 2^level block that holds a live
    /// cell and meets `area`, in no particular order. Blocks are aligned to multiples of 2^level,
    /// so level 0 visits the live cells themselves, and a renderer that shows 2^level cells per
    /// pixel lights one pixel per call. @pre level <= kMaxLevel
    void forEachBlock(UniverseRect area, unsigned level,
                      const std::function<void(UniversePos)>& visit) const;

    /// Nodes in use, the two single cells included.
    [[nodiscard]] std::size_t nodeCount() const noexcept;
    /// The most nodes the memory budget allows.
    [[nodiscard]] std::size_t maxNodes() const noexcept;
    /// Memory the nodes and their hash table take.
    [[nodiscard]] std::uint64_t memoryBytes() const noexcept;
    /// Keeps the nodes the plane needs and drops the rest, remembered results included.
    void collectGarbage();
    /// collectGarbage() once three quarters of the nodes are in use, unless the last collection
    /// left the plane that full. step() calls it itself unless told not to.
    void collectIfFull();

private:
    using NodeId                       = std::uint32_t;
    static constexpr NodeId kNoNode    = ~NodeId{0};
    static constexpr NodeId kDeadCell  = 0;
    static constexpr NodeId kAliveCell = 1;
    /// Nodes per chunk; a chunk is allocated whole and never moves.
    static constexpr std::size_t kChunkNodes = std::size_t{1} << 16;

    /// A square of the plane; see the class comment.
    struct Node
    {
        std::array<NodeId, 4> children{kNoNode, kNoNode, kNoNode, kNoNode};  ///< NW, NE, SW, SE
        std::uint64_t         population = 0;
        NodeId                result     = kNoNode;  ///< For m_resultExponent; kNoNode if unknown
        std::uint8_t          level      = 0;
    };

    /// Thrown when the node budget runs out in the middle of building nodes.
    struct OutOfNodes
    { };
    /// Thrown when the cancel flag of the running step is set.
    struct Cancelled
    { };

    [[nodiscard]] const Node& node(NodeId id) const noexcept;
    [[nodiscard]] Node&       node(NodeId id) noexcept;
    /// The node with these children, made if it does not exist yet. @throws OutOfNodes
    [[nodiscard]] NodeId join(NodeId nw, NodeId ne, NodeId sw, NodeId se);
    [[nodiscard]] NodeId join(const std::array<NodeId, 4>& children);
    [[nodiscard]] NodeId emptyNode(unsigned level);
    /// Puts every node but the two single cells into a new table of `slots` slots.
    void                      rebuildTable(std::size_t slots);
    [[nodiscard]] std::size_t slotOf(const std::array<NodeId, 4>& children) const noexcept;

    /// The root, one level up, with the same centre.
    [[nodiscard]] NodeId expand(NodeId root);
    /// Whether every live cell is in the centre quarter (by width) of `root`.
    [[nodiscard]] bool centred(NodeId root) const noexcept;
    /// The smallest root, at least level 3, that holds the same live cells with the same centre.
    [[nodiscard]] NodeId shrink(NodeId root);
    /// The centre half of `id` after 2^min(m_resultExponent, level - 2) generations.
    [[nodiscard]] NodeId successor(NodeId id);
    [[nodiscard]] NodeId leafSuccessor(const Node& n);
    /// The step itself. @return false if the pattern is too close to the edge of the universe.
    [[nodiscard]] bool advance(unsigned exponent);
    void               forgetResults() noexcept;

    using ExtremeMemo = std::unordered_map<NodeId, UniverseCoord>;
    /// The smallest (`low`) or largest coordinate along `axis` (0 = x, 1 = y) of a live cell of a
    /// non-empty node, relative to its corner. Shared nodes come up many times; `memo` has them.
    [[nodiscard]] UniverseCoord extreme(NodeId id, unsigned axis, bool low,
                                        ExtremeMemo& memo) const;
    void visitBlocks(NodeId id, UniversePos corner, UniverseRect area, unsigned level,
                     const std::function<void(UniversePos)>& visit) const;
    /// A node of `level` cornered at `origin` holding `cells` (all inside it; reordered).
    [[nodiscard]] NodeId build(unsigned level, UniversePos origin, std::span<UniversePos> cells);
    [[nodiscard]] NodeId unite(NodeId a, NodeId b);
    [[nodiscard]] NodeId subtract(NodeId a, NodeId b);
    CellCount            applyCells(std::vector<UniversePos> cells, Cell value);

    Rule m_rule;
    /// 4 × 4 cells (bit y * 4 + x) to their centre 2 × 2 one generation later (bit
    /// (y - 1) * 2 + x - 1).
    std::array<std::uint8_t, 1U << 16> m_leafTable{};

    /// One slot per chunk the budget allows, made once. A chunk is filled whole when its first
    /// node is made and never moves, so readers never see a container change under them.
    std::vector<std::vector<Node>>    m_chunks;
    std::size_t                       m_nodeCount            = 0;
    std::size_t                       m_maxNodes             = 0;
    std::size_t                       m_nodesAfterCollection = 0;  ///< For collectIfFull()
    std::vector<NodeId>               m_table;  ///< Open addressing; kNoNode marks a free slot.
    std::array<NodeId, kMaxLevel + 1> m_emptyNodes{};
    unsigned                          m_resultExponent = 0;        ///< The step the results are for
    const std::atomic<bool>*          m_cancel         = nullptr;  ///< The running step's flag
    std::atomic<NodeId>               m_root{kNoNode};
    std::atomic<std::uint64_t>        m_generation{0};
};

}  // namespace wxLife::core

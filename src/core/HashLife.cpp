#include "wxLife/core/HashLife.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "wxLife/core/Rule.h"
#include "wxLife/core/Types.h"

namespace wxLife::core
{

namespace
{

// Children are listed NW, NE, SW, SE: bit 0 of the index is east, bit 1 is south.
constexpr std::size_t kNW = 0;
constexpr std::size_t kNE = 1;
constexpr std::size_t kSW = 2;
constexpr std::size_t kSE = 3;

/// Hash table slots to start with; always a power of two.
constexpr std::size_t kInitialSlots = std::size_t{1} << 12;

[[nodiscard]] constexpr std::uint64_t mix(std::uint64_t z) noexcept
{
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9U;  // SplitMix64's finaliser
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBU;
    return z ^ (z >> 31);
}

// a + b, clamped to the int64 range, so far-away offsets cannot wrap around into the plane.
[[nodiscard]] constexpr UniverseCoord addClamped(UniverseCoord a, UniverseCoord b) noexcept
{
    constexpr UniverseCoord kMax = std::numeric_limits<UniverseCoord>::max();
    constexpr UniverseCoord kMin = std::numeric_limits<UniverseCoord>::min();
    if (b > 0 && a > kMax - b)
    {
        return kMax;
    }
    if (b < 0 && a < kMin - b)
    {
        return kMin;
    }
    return a + b;
}

[[nodiscard]] constexpr bool insideRadius(UniversePos p) noexcept
{
    return p.x >= -HashLife::kRadius && p.x < HashLife::kRadius && p.y >= -HashLife::kRadius &&
           p.y < HashLife::kRadius;
}

// Whether the square of `size` cells cornered at `corner` meets `area`.
[[nodiscard]] constexpr bool meets(UniverseRect area, UniversePos corner,
                                   UniverseCoord size) noexcept
{
    return corner.x < area.x1 && corner.y < area.y1 && corner.x + size > area.x0 &&
           corner.y + size > area.y0;
}

[[nodiscard]] constexpr UniverseCoord side(unsigned level) noexcept
{
    return UniverseCoord{1} << level;
}

// The centre 2 × 2 of a 4 × 4 square (bit y * 4 + x) one generation on (bit (y - 1) * 2 + x - 1).
[[nodiscard]] constexpr std::uint8_t leafResult(const Rule& rule, std::uint32_t bits) noexcept
{
    const auto alive  = [bits](int x, int y) { return ((bits >> ((y * 4) + x)) & 1U) != 0; };
    unsigned   result = 0;
    for (int y = 1; y <= 2; ++y)
    {
        for (int x = 1; x <= 2; ++x)
        {
            unsigned neighbours = 0;
            for (const auto& [dx, dy] :
                 {std::pair{-1, -1}, std::pair{0, -1}, std::pair{1, -1}, std::pair{-1, 0},
                  std::pair{1, 0}, std::pair{-1, 1}, std::pair{0, 1}, std::pair{1, 1}})
            {
                neighbours += alive(x + dx, y + dy) ? 1U : 0U;
            }
            const unsigned next = rule.nextState(alive(x, y), neighbours);
            result |= next << static_cast<unsigned>(((y - 1) * 2) + (x - 1));
        }
    }
    return static_cast<std::uint8_t>(result);
}

}  // namespace

HashLife::HashLife(const Rule& rule, std::uint64_t memoryBudgetBytes) : m_rule(rule)
{
    assert(supports(rule));
    // A node, and its share of a hash table that is at most half full and briefly doubled.
    constexpr std::uint64_t kBytesPerNode = sizeof(Node) + (4 * sizeof(NodeId));
    m_maxNodes                            = static_cast<std::size_t>(std::clamp<std::uint64_t>(
        memoryBudgetBytes / kBytesPerNode, kChunkNodes, std::uint64_t{kNoNode} - 1));
    m_chunks.reserve((m_maxNodes + kChunkNodes - 1) / kChunkNodes);
    setRule(rule);
    clear();
}

const Rule& HashLife::rule() const noexcept
{
    return m_rule;
}

void HashLife::setRule(const Rule& rule)
{
    assert(supports(rule));
    m_rule = rule;
    for (std::uint32_t bits = 0; bits < m_leafTable.size(); ++bits)
    {
        m_leafTable.at(bits) = leafResult(m_rule, bits);
    }
    forgetResults();
}

std::uint64_t HashLife::generation() const noexcept
{
    return m_generation;
}

std::uint64_t HashLife::population() const noexcept
{
    return node(m_root).population;
}

Cell HashLife::at(UniversePos p) const noexcept
{
    NodeId              id    = m_root;
    unsigned            level = node(id).level;
    const UniverseCoord half  = side(level - 1);
    UniverseCoord       left  = -half;
    UniverseCoord       top   = -half;
    if (p.x < left || p.y < top || p.x >= half || p.y >= half)
    {
        return kDead;
    }
    while (level > 0)
    {
        if (node(id).population == 0)
        {
            return kDead;
        }
        const UniverseCoord childSide = side(level - 1);
        const bool          east      = p.x >= left + childSide;
        const bool          south     = p.y >= top + childSide;
        id                            = node(id).children.at((south ? 2U : 0U) + (east ? 1U : 0U));
        left += east ? childSide : 0;
        top += south ? childSide : 0;
        --level;
    }
    return id == kAliveCell ? kAlive : kDead;
}

std::optional<UniverseRect> HashLife::bounds() const
{
    if (population() == 0)
    {
        return std::nullopt;
    }
    const UniverseCoord          corner = -side(node(m_root).level - 1U);
    std::array<UniverseCoord, 4> edges{};  // left, right, top, bottom
    for (unsigned i = 0; i < 4; ++i)
    {
        ExtremeMemo memo;
        edges.at(i) = corner + extreme(m_root, i / 2, i % 2 == 0, memo);
    }
    return UniverseRect{.x0 = edges[0], .y0 = edges[2], .x1 = edges[1] + 1, .y1 = edges[3] + 1};
}

CellCount HashLife::setCells(std::span<const UniversePos> cells, Cell value, UniversePos offset)
{
    std::vector<UniversePos> moved;
    moved.reserve(cells.size());
    for (const UniversePos cell : cells)
    {
        const UniversePos p{.x = addClamped(cell.x, offset.x), .y = addClamped(cell.y, offset.y)};
        if (insideRadius(p))
        {
            moved.push_back(p);
        }
    }
    return applyCells(std::move(moved), value);
}

CellCount HashLife::setCells(std::span<const CellPos> cells, Cell value, UniversePos offset)
{
    std::vector<UniversePos> moved;
    moved.reserve(cells.size());
    for (const CellPos cell : cells)
    {
        const UniversePos p{.x = addClamped(cell.x, offset.x), .y = addClamped(cell.y, offset.y)};
        if (insideRadius(p))
        {
            moved.push_back(p);
        }
    }
    return applyCells(std::move(moved), value);
}

void HashLife::clear()
{
    m_chunks.clear();
    m_nodeCount            = 0;
    m_nodesAfterCollection = 0;
    // The two single cells have fixed ids and stay out of the hash table.
    m_chunks.emplace_back();
    m_chunks.back().reserve(kChunkNodes);
    m_chunks.back().push_back({.population = 0, .level = 0});
    m_chunks.back().push_back({.population = 1, .level = 0});
    m_nodeCount = 2;
    m_table.assign(kInitialSlots, kNoNode);
    m_emptyNodes.fill(kNoNode);
    m_emptyNodes[0] = kDeadCell;
    m_root          = emptyNode(3);
    m_generation    = 0;
}

std::expected<void, HashLifeError> HashLife::step(unsigned exponent)
{
    assert(exponent <= kMaxStepExponent);
    const std::uint64_t generations = std::uint64_t{1} << exponent;
    if (population() == 0)
    {
        m_generation += generations;  // an empty plane stays empty
        return {};
    }
    collectIfFull();
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        try
        {
            if (!advance(exponent))
            {
                return std::unexpected(HashLifeError::UNIVERSE_EDGE);
            }
            m_generation += generations;
            return {};
        }
        catch (const OutOfNodes&)  // NOLINT(bugprone-empty-catch): retried below
        {
        }
        catch (const std::bad_alloc&)  // NOLINT(bugprone-empty-catch): retried below
        {
        }
        // The root has not changed; everything the failed step built is garbage.
        collectGarbage();
    }
    return std::unexpected(HashLifeError::OUT_OF_MEMORY);
}

void HashLife::forEachBlock(UniverseRect area, unsigned level,
                            const std::function<void(UniversePos)>& visit) const
{
    assert(level <= kMaxLevel);
    const Node&         root = node(m_root);
    const UniverseCoord half = side(root.level - 1U);
    if (level < root.level)
    {
        visitBlocks(m_root, {.x = -half, .y = -half}, area, level, visit);
        return;
    }
    // Blocks at least as large as the root: the root straddles the centre, so each of its
    // quadrants lies in a block of its own.
    const UniverseCoord block = side(level);
    for (std::size_t q = 0; q < 4; ++q)
    {
        const UniversePos corner{.x = (q & 1U) != 0 ? 0 : -block, .y = (q & 2U) != 0 ? 0 : -block};
        if (node(root.children.at(q)).population > 0 && meets(area, corner, block))
        {
            visit(corner);
        }
    }
}

std::size_t HashLife::nodeCount() const noexcept
{
    return m_nodeCount;
}

std::size_t HashLife::maxNodes() const noexcept
{
    return m_maxNodes;
}

void HashLife::collectGarbage()
{
    // Mark what the root needs.
    std::vector<std::uint8_t> keep(m_nodeCount, 0);
    keep[kDeadCell]  = 1;
    keep[kAliveCell] = 1;
    std::vector<NodeId> pending{m_root};
    while (!pending.empty())
    {
        const NodeId id = pending.back();
        pending.pop_back();
        if (keep[id] != 0)
        {
            continue;
        }
        keep[id] = 1;
        for (const NodeId child : node(id).children)
        {
            if (child != kNoNode && keep[child] == 0)
            {
                pending.push_back(child);
            }
        }
    }

    // Move the survivors down in id order. A node's children are older than the node, so they
    // have their new ids by the time the node moves; results are forgotten.
    std::vector<NodeId> moved(m_nodeCount, kNoNode);
    NodeId              next = 0;
    for (NodeId id = 0; id < m_nodeCount; ++id)
    {
        if (keep[id] == 0)
        {
            continue;
        }
        Node survivor = node(id);
        for (NodeId& child : survivor.children)
        {
            child = child == kNoNode ? kNoNode : moved[child];
        }
        survivor.result = kNoNode;
        moved[id]       = next;
        node(next)      = survivor;
        ++next;
    }
    m_root      = moved[m_root];
    m_nodeCount = next;
    m_chunks.resize((m_nodeCount + kChunkNodes - 1) / kChunkNodes);
    const std::size_t inLast = m_nodeCount - ((m_chunks.size() - 1) * kChunkNodes);
    m_chunks.back().resize(inLast);

    m_emptyNodes.fill(kNoNode);
    m_emptyNodes[0]   = kDeadCell;
    std::size_t slots = kInitialSlots;
    while (slots < 2 * m_nodeCount)
    {
        slots *= 2;
    }
    rebuildTable(slots);
    m_nodesAfterCollection = m_nodeCount;
}

const HashLife::Node& HashLife::node(NodeId id) const noexcept
{
    return m_chunks[id / kChunkNodes][id % kChunkNodes];
}

HashLife::Node& HashLife::node(NodeId id) noexcept
{
    return m_chunks[id / kChunkNodes][id % kChunkNodes];
}

HashLife::NodeId HashLife::join(NodeId nw, NodeId ne, NodeId sw, NodeId se)
{
    return join({nw, ne, sw, se});
}

HashLife::NodeId HashLife::join(const std::array<NodeId, 4>& children)
{
    const std::size_t mask = m_table.size() - 1;
    std::size_t       slot = slotOf(children);
    while (m_table[slot] != kNoNode)
    {
        if (node(m_table[slot]).children == children)
        {
            return m_table[slot];
        }
        slot = (slot + 1) & mask;
    }

    if (m_nodeCount >= m_maxNodes)
    {
        throw OutOfNodes{};
    }
    std::uint64_t population = 0;
    for (const NodeId child : children)
    {
        population += node(child).population;
    }
    if (m_nodeCount % kChunkNodes == 0)
    {
        m_chunks.emplace_back();  // within the capacity reserved up front, so no chunk moves
        m_chunks.back().reserve(kChunkNodes);
    }
    const auto id = static_cast<NodeId>(m_nodeCount);
    m_chunks.back().push_back({.children   = children,
                               .population = population,
                               .result     = kNoNode,
                               .level = static_cast<std::uint8_t>(node(children[0]).level + 1)});
    ++m_nodeCount;
    m_table[slot] = id;
    if (2 * (m_nodeCount - 2) > m_table.size())  // the two single cells are not in the table
    {
        rebuildTable(2 * m_table.size());
    }
    return id;
}

HashLife::NodeId HashLife::emptyNode(unsigned level)
{
    NodeId& empty = m_emptyNodes.at(level);
    if (empty == kNoNode)
    {
        const NodeId below = emptyNode(level - 1);
        empty              = join(below, below, below, below);
    }
    return empty;
}

void HashLife::rebuildTable(std::size_t slots)
{
    std::vector<NodeId> table(slots, kNoNode);
    const std::size_t   mask = slots - 1;
    m_table.swap(table);  // slotOf() reads the new size
    for (NodeId id = 2; id < m_nodeCount; ++id)
    {
        std::size_t slot = slotOf(node(id).children);
        while (m_table[slot] != kNoNode)
        {
            slot = (slot + 1) & mask;
        }
        m_table[slot] = id;
    }
}

std::size_t HashLife::slotOf(const std::array<NodeId, 4>& children) const noexcept
{
    const std::uint64_t west = (std::uint64_t{children[kNW]} << 32U) | children[kSW];
    const std::uint64_t east = (std::uint64_t{children[kNE]} << 32U) | children[kSE];
    return mix(mix(west) ^ east) & (m_table.size() - 1);
}

HashLife::NodeId HashLife::expand(NodeId root)
{
    const Node   r = node(root);
    const NodeId e = emptyNode(r.level - 1U);
    return join(join(e, e, e, r.children[kNW]), join(e, e, r.children[kNE], e),
                join(e, r.children[kSW], e, e), join(r.children[kSE], e, e, e));
}

bool HashLife::centred(NodeId root) const noexcept
{
    const Node& r = node(root);
    if (r.level < 3)
    {
        return false;
    }
    // Each quadrant's live cells must all be in its innermost grandchild's innermost child.
    const auto inner = [&](std::size_t quadrant) {
        const std::size_t towardsCentre = kSE - quadrant;  // NW → SE, NE → SW, SW → NE, SE → NW
        const Node&       q             = node(r.children.at(quadrant));
        const Node&       qq            = node(q.children.at(towardsCentre));
        return q.population == node(qq.children.at(towardsCentre)).population;
    };
    return inner(kNW) && inner(kNE) && inner(kSW) && inner(kSE);
}

HashLife::NodeId HashLife::shrink(NodeId root)
{
    while (node(root).level > 3)
    {
        const Node&           r = node(root);
        std::array<NodeId, 4> centre{};
        bool                  inside = true;
        for (std::size_t q = 0; q < 4; ++q)
        {
            const Node& quadrant = node(r.children.at(q));
            centre.at(q)         = quadrant.children.at(kSE - q);
            inside               = inside && quadrant.population == node(centre.at(q)).population;
        }
        if (!inside)
        {
            break;
        }
        root = join(centre);
    }
    return root;
}

HashLife::NodeId HashLife::successor(NodeId id)
{
    const Node n = node(id);
    if (n.result != kNoNode)
    {
        return n.result;
    }
    NodeId result = kNoNode;
    if (n.population == 0)
    {
        result = emptyNode(n.level - 1U);
    }
    else if (n.level == 2)
    {
        result = leafSuccessor(n);
    }
    else
    {
        const Node a = node(n.children[kNW]);
        const Node b = node(n.children[kNE]);
        const Node c = node(n.children[kSW]);
        const Node d = node(n.children[kSE]);
        // The nine overlapping squares one level down, row by row, each advanced.
        const std::array<NodeId, 9> nine{
            n.children[kNW],
            join(a.children[kNE], b.children[kNW], a.children[kSE], b.children[kSW]),
            n.children[kNE],
            join(a.children[kSW], a.children[kSE], c.children[kNW], c.children[kNE]),
            join(a.children[kSE], b.children[kSW], c.children[kNE], d.children[kNW]),
            join(b.children[kSW], b.children[kSE], d.children[kNW], d.children[kNE]),
            n.children[kSW],
            join(c.children[kNE], d.children[kNW], c.children[kSE], d.children[kSW]),
            n.children[kSE],
        };
        std::array<NodeId, 9> r{};
        for (std::size_t i = 0; i < nine.size(); ++i)
        {
            r.at(i) = successor(nine.at(i));
        }
        // The four squares of the result, each made of four of the nine.
        constexpr std::array<std::array<std::size_t, 4>, 4> kQuads{
            {{0, 1, 3, 4}, {1, 2, 4, 5}, {3, 4, 6, 7}, {4, 5, 7, 8}}};
        std::array<NodeId, 4> quads{};
        const bool            fullStep = m_resultExponent >= n.level - 2U;
        for (std::size_t q = 0; q < 4; ++q)
        {
            const auto& [nw, ne, sw, se] = kQuads.at(q);
            if (fullStep)
            {
                // Advance the quarter again: 2^(level-3) twice is 2^(level-2) generations.
                quads.at(q) = successor(join(r.at(nw), r.at(ne), r.at(sw), r.at(se)));
            }
            else
            {
                // Already 2^exponent generations on: just take the centre of the four.
                quads.at(q) = join(node(r.at(nw)).children[kSE], node(r.at(ne)).children[kSW],
                                   node(r.at(sw)).children[kNE], node(r.at(se)).children[kNW]);
            }
        }
        result = join(quads);
    }
    node(id).result = result;
    return result;
}

HashLife::NodeId HashLife::leafSuccessor(const Node& n)
{
    // Gather the 4 × 4 cells: bit y * 4 + x.
    unsigned bits = 0;
    for (std::size_t q = 0; q < 4; ++q)
    {
        const Node& quadrant = node(n.children.at(q));
        const auto  qx       = static_cast<unsigned>((q & 1U) * 2);
        const auto  qy       = static_cast<unsigned>((q >> 1U) * 2);
        for (std::size_t s = 0; s < 4; ++s)
        {
            if (quadrant.children.at(s) == kAliveCell)
            {
                bits |= 1U << (((qy + (s >> 1U)) * 4) + qx + (s & 1U));
            }
        }
    }
    const unsigned centre = m_leafTable.at(bits);
    const auto     cell   = [centre](unsigned bit) {
        return ((centre >> bit) & 1U) != 0 ? kAliveCell : kDeadCell;
    };
    return join(cell(0), cell(1), cell(2), cell(3));
}

bool HashLife::advance(unsigned exponent)
{
    if (exponent != m_resultExponent)
    {
        forgetResults();
        m_resultExponent = exponent;
    }
    // Room for 2^exponent generations of growth at the speed of light: the pattern in the centre
    // quarter of a root at least three levels above the step, so the result's centre half holds
    // everything the step makes.
    NodeId root = m_root;
    while (node(root).level < exponent + 3 || !centred(root))
    {
        if (node(root).level >= kMaxLevel)
        {
            return false;
        }
        root = expand(root);
    }
    m_root = shrink(successor(root));
    return true;
}

void HashLife::forgetResults() noexcept
{
    for (std::vector<Node>& chunk : m_chunks)
    {
        for (Node& n : chunk)
        {
            n.result = kNoNode;
        }
    }
}

void HashLife::collectIfFull()
{
    // Three quarters full, and grown since the last collection, which may have left a large
    // pattern that simply needs the room.
    if (m_nodeCount > (m_maxNodes / 4) * 3 &&
        m_nodeCount > m_nodesAfterCollection + (m_maxNodes / 8))
    {
        collectGarbage();
    }
}

UniverseCoord HashLife::extreme(NodeId id, unsigned axis, bool low, ExtremeMemo& memo) const
{
    const Node& n = node(id);
    if (n.level == 0)
    {
        return 0;
    }
    if (const auto found = memo.find(id); found != memo.end())
    {
        return found->second;
    }
    // The near half along the axis decides, unless it is empty; bit 0 of a child's index is east,
    // bit 1 south.
    const UniverseCoord half  = side(n.level - 1U);
    UniverseCoord       best  = 0;
    bool                found = false;
    for (const unsigned far : low ? std::array{0U, 1U} : std::array{1U, 0U})
    {
        for (std::size_t q = 0; q < 4; ++q)
        {
            const auto   bit   = static_cast<unsigned>(axis == 0 ? (q & 1U) : (q >> 1U));
            const NodeId child = n.children.at(q);
            if (bit != far || node(child).population == 0)
            {
                continue;
            }
            const UniverseCoord v = (far * half) + extreme(child, axis, low, memo);
            if (!found)
            {
                best = v;
            }
            best  = low ? std::min(best, v) : std::max(best, v);
            found = true;
        }
        if (found)
        {
            break;
        }
    }
    memo.emplace(id, best);
    return best;
}

void HashLife::visitBlocks(NodeId id, UniversePos corner, UniverseRect area, unsigned level,
                           const std::function<void(UniversePos)>& visit) const
{
    const Node& n = node(id);
    if (n.population == 0 || !meets(area, corner, side(n.level)))
    {
        return;
    }
    if (n.level == level)
    {
        visit(corner);
        return;
    }
    const UniverseCoord half = side(n.level - 1U);
    for (std::size_t q = 0; q < 4; ++q)
    {
        visitBlocks(n.children.at(q),
                    {.x = corner.x + ((q & 1U) != 0 ? half : 0),
                     .y = corner.y + ((q & 2U) != 0 ? half : 0)},
                    area, level, visit);
    }
}

HashLife::NodeId HashLife::build(unsigned level, UniversePos origin, std::span<UniversePos> cells)
{
    if (cells.empty())
    {
        return emptyNode(level);
    }
    if (level == 0)
    {
        return kAliveCell;
    }
    const UniverseCoord half = side(level - 1);
    const UniverseCoord midX = origin.x + half;
    const UniverseCoord midY = origin.y + half;
    // std::ranges::partition() returns the part that fails the test.
    const auto                   isNorth = [&](UniversePos p) { return p.y < midY; };
    const auto                   isWest  = [&](UniversePos p) { return p.x < midX; };
    const std::span<UniversePos> south   = std::ranges::partition(cells, isNorth);
    const std::span<UniversePos> north(cells.begin(), south.begin());
    const std::span<UniversePos> northEast = std::ranges::partition(north, isWest);
    const std::span<UniversePos> southEast = std::ranges::partition(south, isWest);
    return join(build(level - 1, origin, {north.begin(), northEast.begin()}),
                build(level - 1, {.x = midX, .y = origin.y}, northEast),
                build(level - 1, {.x = origin.x, .y = midY}, {south.begin(), southEast.begin()}),
                build(level - 1, {.x = midX, .y = midY}, southEast));
}

HashLife::NodeId HashLife::unite(NodeId a, NodeId b)
{
    const Node na = node(a);
    const Node nb = node(b);
    if (a == b || nb.population == 0)
    {
        return a;
    }
    if (na.population == 0 || na.level == 0)
    {
        return b;  // at level 0 both are alive by now
    }
    return join(
        unite(na.children[kNW], nb.children[kNW]), unite(na.children[kNE], nb.children[kNE]),
        unite(na.children[kSW], nb.children[kSW]), unite(na.children[kSE], nb.children[kSE]));
}

HashLife::NodeId HashLife::subtract(NodeId a, NodeId b)
{
    const Node na = node(a);
    const Node nb = node(b);
    if (na.population == 0 || nb.population == 0)
    {
        return a;
    }
    if (a == b)
    {
        return emptyNode(na.level);
    }
    return join(
        subtract(na.children[kNW], nb.children[kNW]), subtract(na.children[kNE], nb.children[kNE]),
        subtract(na.children[kSW], nb.children[kSW]), subtract(na.children[kSE], nb.children[kSE]));
}

CellCount HashLife::applyCells(std::vector<UniversePos> cells, Cell value)
{
    assert(value == kDead || value == kAlive);
    if (cells.empty())
    {
        return 0;
    }
    const auto [left, right] = std::ranges::minmax(cells, {}, &UniversePos::x);
    const auto [top, bottom] = std::ranges::minmax(cells, {}, &UniversePos::y);
    collectIfFull();
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        try
        {
            // A root that holds every cell; kRadius guarantees one no higher than kMaxLevel.
            NodeId root = m_root;
            while (true)
            {
                const UniverseCoord half = side(node(root).level - 1U);
                if (left.x >= -half && top.y >= -half && right.x < half && bottom.y < half)
                {
                    break;
                }
                root = expand(root);
            }
            const unsigned      level = node(root).level;
            const UniverseCoord half  = side(level - 1);
            const NodeId        stamp = build(level, {.x = -half, .y = -half}, cells);
            const NodeId        next = value == kAlive ? unite(root, stamp) : subtract(root, stamp);
            const auto          before = static_cast<CellCount>(population());
            m_root                     = shrink(next);
            const auto after           = static_cast<CellCount>(population());
            return value == kAlive ? after - before : before - after;
        }
        catch (const OutOfNodes&)  // NOLINT(bugprone-empty-catch): retried below
        {
        }
        catch (const std::bad_alloc&)  // NOLINT(bugprone-empty-catch): retried below
        {
        }
        collectGarbage();
    }
    throw std::bad_alloc();
}

}  // namespace wxLife::core

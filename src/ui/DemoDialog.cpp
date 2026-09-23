#include "wxLife/ui/DemoDialog.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <wx/bitmap.h>
#include <wx/font.h>
#include <wx/image.h>
#include <wx/sizer.h>

#include "wxLife/core/Ant.h"
#include "wxLife/core/Demo.h"
#include "wxLife/core/Format.h"
#include "wxLife/core/HashLife.h"
#include "wxLife/core/Pattern.h"
#include "wxLife/core/Rule.h"
#include "wxLife/core/Speed.h"
#include "wxLife/core/Types.h"
#include "wxLife/core/WorldLimits.h"
#include "wxLife/render/PixelBuffer.h"
#include "wxLife/render/RenderStyle.h"
#include "wxLife/render/Thumbnail.h"
#include "wxLife/render/Types.h"
#include "wxLife/ui/Theme.h"
#include "wxLife/ui/WxConvert.h"

namespace wxLife::ui
{

namespace
{

constexpr int kTreeWidthDip     = 250;
constexpr int kTreeHeightDip    = 480;
constexpr int kDetailsWidthDip  = 400;
constexpr int kPreviewHeightDip = 220;
/// Lines kept free for the longest description, so the dialog does not change size.
constexpr int kAboutLines = 8;
/// Enough for the largest demo's own nodes; the preview never steps.
constexpr std::uint64_t kPreviewMemory = std::uint64_t{256} << 20;

[[nodiscard]] std::string countText(std::int64_t n)
{
    return core::formatCount(static_cast<std::uint64_t>(n));
}

[[nodiscard]] std::string sizeText(core::Extent extent)
{
    return std::format("{} × {}", countText(extent.width), countText(extent.height));
}

[[nodiscard]] constexpr std::string_view edgesText(core::Topology topology) noexcept
{
    switch (topology)
    {
        case core::Topology::BOUNDED:
            return "dead edges";
        case core::Topology::TORUS:
            return "wrapping edges";
    }
    std::unreachable();
}

// The pattern line and the world line of the details.
[[nodiscard]] std::string factsText(const core::Demo& demo, const core::Pattern& pattern)
{
    std::string what;
    if (demo.automaton == core::Automaton::LANGTON_ANT && pattern.cells.empty())
    {
        what = std::format("{} ant{} on an empty world", demo.ants.size(),
                           demo.ants.size() == 1 ? "" : "s");
    }
    else if (pattern.tree)
    {
        const core::UniverseRect bounds = pattern.tree->bounds;
        what = std::format("Pattern: {} × {} cells, {} alive", countText(bounds.x1 - bounds.x0),
                           countText(bounds.y1 - bounds.y0),
                           core::formatCount(pattern.tree->population));
    }
    else
    {
        what = std::format("Pattern: {} cells, {} alive", sizeText(pattern.extent),
                           countText(static_cast<std::int64_t>(pattern.cells.size())));
    }
    const std::string rule = pattern.rule.value_or(core::Rule{}).toString();
    if (demo.kind == core::WorldKind::UNBOUNDED)
    {
        return what + "\n" +
               std::format("World: unbounded · {} · {} · steps of 2^{}", rule,
                           core::toString(demo.speed), demo.stepExponent);
    }
    std::string world =
        std::format("World: {} · {}", sizeText(demo.world), edgesText(demo.topology));
    if (demo.automaton == core::Automaton::LIFE)
    {
        world += std::format(" · {}", rule);
    }
    world += std::format(" · {} · {}", core::toString(demo.speed),
                         core::formatBytes(core::worldBytes(demo.world)));
    return what + "\n" + world;
}

}  // namespace

std::optional<std::size_t> DemoDialog::ask(wxWindow* parent, std::optional<std::size_t> selected,
                                           std::uint64_t memoryBudgetBytes)
{
    DemoDialog dialog(parent, selected, memoryBudgetBytes);
    if (dialog.ShowModal() != wxID_OK)
    {
        return std::nullopt;
    }
    return dialog.selectedDemo();
}

DemoDialog::DemoDialog(wxWindow* parent, std::optional<std::size_t> selected,
                       std::uint64_t memoryBudgetBytes)
  : wxDialog(parent, wxID_ANY, "Demo Patterns", wxDefaultPosition, wxDefaultSize,
             wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
    m_budget(memoryBudgetBytes),
    m_patterns(core::demos().size())
{
    m_tree                  = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition,
                                             FromDIP(wxSize(kTreeWidthDip, kTreeHeightDip)),
                                             wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxTR_SINGLE);
    const wxTreeItemId root = m_tree->AddRoot(wxString());
    for (const core::DemoCategory category : core::kDemoCategories)
    {
        const wxTreeItemId group = m_tree->AppendItem(root, toWx(core::toString(category)));
        m_tree->SetItemBold(group);
        for (const core::Demo& demo : core::demos())
        {
            if (demo.category == category)
            {
                m_items.push_back(m_tree->AppendItem(group, toWx(demo.name)));
            }
        }
    }
    m_tree->ExpandAll();

    const int detailsWidth = FromDIP(kDetailsWidthDip);
    m_name                 = new wxStaticText(this, wxID_ANY, wxString());
    m_name->SetFont(m_name->GetFont().Bold().Larger());
    m_credit  = new wxStaticText(this, wxID_ANY, wxString());
    m_preview = new wxStaticBitmap(this, wxID_ANY, wxBitmap());
    m_preview->SetMinSize(wxSize(detailsWidth, FromDIP(kPreviewHeightDip)));
    m_facts = new wxStaticText(this, wxID_ANY, wxString());
    m_about = new wxStaticText(this, wxID_ANY, wxString());
    m_about->SetMinSize(wxSize(detailsWidth, GetCharHeight() * kAboutLines));
    m_error = new wxStaticText(this, wxID_ANY, wxString());
    useErrorColour(*m_error);

    wxStdDialogButtonSizer* buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);

    m_load = buttons->GetAffirmativeButton();
    m_load->SetLabel("Load");

    auto* details = new wxBoxSizer(wxVERTICAL);
    details->Add(m_name, wxSizerFlags().Border(wxBOTTOM));
    details->Add(m_credit, wxSizerFlags().Border(wxBOTTOM));
    details->Add(m_preview, wxSizerFlags().Expand().Border(wxBOTTOM));
    details->Add(m_facts, wxSizerFlags().Border(wxBOTTOM));
    details->Add(m_about, wxSizerFlags(1).Expand());
    details->Add(m_error, wxSizerFlags());

    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(m_tree, wxSizerFlags(1).Expand());
    row->Add(details, wxSizerFlags().Expand().DoubleBorder(wxLEFT));

    auto* column = new wxBoxSizer(wxVERTICAL);
    column->Add(row, wxSizerFlags(1).Expand().DoubleBorder());
    column->Add(buttons, wxSizerFlags().Expand().DoubleBorder(wxLEFT | wxRIGHT | wxBOTTOM));
    SetSizerAndFit(column);
    CentreOnParent();

    m_tree->Bind(wxEVT_TREE_SEL_CHANGED, [this](wxTreeEvent&) { showSelection(); });
    // Double click or Enter on a demo loads it; on a category, the tree opens or closes it.
    m_tree->Bind(wxEVT_TREE_ITEM_ACTIVATED, [this](wxTreeEvent& event) {
        if (!selectedDemo())
        {
            event.Skip();
            return;
        }
        if (Validate())
        {
            EndModal(wxID_OK);
        }
    });

    const std::size_t first = std::min(selected.value_or(0), m_items.size() - 1);
    m_tree->SelectItem(m_items.at(first));
    m_tree->EnsureVisible(m_items.at(first));
    m_tree->SetFocus();
    showSelection();
}

bool DemoDialog::Validate()
{
    const std::optional<std::size_t> demo = selectedDemo();
    return wxDialog::Validate() && demo && fits(*demo);
}

std::optional<std::size_t> DemoDialog::selectedDemo() const
{
    const wxTreeItemId selection = m_tree->GetSelection();
    const auto         found     = std::ranges::find(m_items, selection);
    if (!selection.IsOk() || found == m_items.end())
    {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::ranges::distance(m_items.begin(), found));
}

bool DemoDialog::fits(std::size_t demo) const
{
    const core::Demo& chosen = core::demos()[demo];
    return chosen.kind == core::WorldKind::UNBOUNDED ||
           core::validateExtent(chosen.world, m_budget).has_value();
}

const core::Pattern& DemoDialog::pattern(std::size_t demo)
{
    std::optional<core::Pattern>& cached = m_patterns.at(demo);
    if (!cached)
    {
        cached = core::demoPattern(core::demos()[demo]);
    }
    return *cached;
}

void DemoDialog::showSelection()
{
    const std::optional<std::size_t> selected = selectedDemo();
    if (!selected)
    {
        m_load->Enable(false);  // a category: keep showing the last demo
        return;
    }
    const core::Demo&    demo    = core::demos()[*selected];
    const core::Pattern& pattern = this->pattern(*selected);
    const int            width   = FromDIP(kDetailsWidthDip);

    m_name->SetLabelText(toWx(demo.name));
    m_credit->SetLabelText(toWx(demo.credit));
    m_facts->SetLabelText(toWx(factsText(demo, pattern)));
    m_about->SetLabelText(toWx(demo.about));
    m_about->Wrap(width);
    // A plane's memory grows as it runs, so a demo that needs more than the budget is only warned
    // about; a fixed-size world that does not fit cannot be loaded at all.
    std::string problem;
    if (demo.kind == core::WorldKind::UNBOUNDED)
    {
        if (demo.memoryNeeded > m_budget)
        {
            problem = std::format(
                "It needs about {} of memory to run smoothly, and the memory budget is {}: it "
                "may run slowly or stop.",
                core::formatBytes(demo.memoryNeeded), core::formatBytes(m_budget));
        }
    }
    else if (const auto valid = core::validateExtent(demo.world, m_budget); !valid)
    {
        problem =
            "Too large for this computer. " + core::describe(valid.error(), demo.world, m_budget);
    }
    m_error->SetLabelText(toWx(problem));
    m_error->Wrap(width);
    m_load->Enable(fits(*selected));
    showPreview(*selected);
    Layout();
}

void DemoDialog::showPreview(std::size_t demo)
{
    const core::Demo&    selected = core::demos()[demo];
    const core::Pattern& pattern  = this->pattern(demo);
    // Device pixels, so the picture is sharp on a HiDPI screen too.
    const double              scale = GetContentScaleFactor();
    const wxSize              box   = m_preview->GetMinSize();
    const render::PixelSize   maxSize{.width  = std::lround(box.x * scale),
                                      .height = std::lround(box.y * scale)};
    const render::RenderStyle style = themeStyle();

    // An ant demo shows its world with the ants; any other demo shows its pattern alone.
    const bool                     ants = selected.automaton == core::Automaton::LANGTON_ANT;
    std::vector<core::CellPos>     cells;
    std::span<const core::CellPos> shown = pattern.cells;
    core::Extent                   area  = pattern.extent;
    if (ants)
    {
        const core::CellPos origin =
            selected.origin.value_or(core::CellPos{.x = (selected.world.width - area.width) / 2,
                                                   .y = (selected.world.height - area.height) / 2});
        cells.reserve(pattern.cells.size());
        for (const core::CellPos cell : pattern.cells)
        {
            cells.push_back({.x = cell.x + origin.x, .y = cell.y + origin.y});
        }
        shown = cells;
        area  = selected.world;
    }
    render::PixelBuffer thumbnail;
    if (pattern.tree)
    {
        // Far too many cells to list: the plane draws them block by block.
        core::HashLife plane(pattern.rule.value_or(core::Rule{}), kPreviewMemory);
        plane.load(*pattern.tree);
        render::drawThumbnail(plane, pattern.tree->bounds, maxSize, style, thumbnail);
    }
    else
    {
        render::drawThumbnail(shown, ants ? selected.ants : std::span<const core::Ant>{}, area,
                              maxSize, style, thumbnail);
    }

    // The thumbnail, centred on a box of the colour beyond a world's edge.
    wxImage image(static_cast<int>(maxSize.width), static_cast<int>(maxSize.height));
    image.SetRGB(wxRect(0, 0, image.GetWidth(), image.GetHeight()), style.outside.r,
                 style.outside.g, style.outside.b);
    const render::PixelSize size = thumbnail.size();
    if (size.width > 0 && size.height > 0)
    {
        const wxImage picture(static_cast<int>(size.width), static_cast<int>(size.height));
        std::ranges::copy(thumbnail.bytes(), picture.GetData());
        image.Paste(picture, static_cast<int>((maxSize.width - size.width) / 2),
                    static_cast<int>((maxSize.height - size.height) / 2));
    }
    m_preview->SetBitmap(wxBitmap(image, -1, scale));
}

}  // namespace wxLife::ui

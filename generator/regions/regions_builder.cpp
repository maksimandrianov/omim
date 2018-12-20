#include "generator/regions/regions_builder.hpp"

#include "base/assert.hpp"
#include "base/stl_helpers.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <numeric>
#include <queue>
#include <thread>
#include <unordered_set>

#include "3party/ThreadPool/ThreadPool.h"

namespace generator
{
namespace regions
{
namespace
{
Node::Ptr ShrinkToFit(Node::Ptr p)
{
  p->ShrinkToFitChildren();
  for (auto ptr : p->GetChildren())
    ShrinkToFit(ptr);

  return p;
}
}  // namespace

RegionsBuilder::RegionsBuilder(Regions && regions,
                               std::unique_ptr<ToStringPolicyInterface> toStringPolicy,
                               int cpuCount)
  : m_toStringPolicy(std::move(toStringPolicy))
  , m_regions(std::move(regions))
  , m_cpuCount(cpuCount)
{
  ASSERT(m_toStringPolicy, ());
  ASSERT(m_cpuCount != 0, ());


  auto const isCountry = [](Region const & r) { return r.IsCountry(); };
  std::copy_if(std::begin(m_regions), std::end(m_regions), std::back_inserter(m_countries), isCountry);
  base::EraseIf(m_regions, isCountry);
  auto const cmp = [](Region const & l, Region const & r) { return l.GetArea() > r.GetArea(); };
  std::sort(std::begin(m_countries), std::end(m_countries), cmp);
}

RegionsBuilder::RegionsBuilder(Regions && regions, int cpuCount)
  : RegionsBuilder(std::move(regions), std::make_unique<JsonPolicy>(), cpuCount) {}

RegionsBuilder::Regions const & RegionsBuilder::GetCountries() const
{
  return m_countries;
}

RegionsBuilder::StringsList RegionsBuilder::GetCountryNames() const
{
  StringsList result;
  std::unordered_set<std::string> set;
  for (auto const & c : GetCountries())
  {
    auto name = c.GetName();
    if (set.insert(name).second)
      result.emplace_back(std::move(name));
  }

  return result;
}

RegionsBuilder::CountryTrees const & RegionsBuilder::GetCountryTrees() const
{
  return m_countryTrees;
}

RegionsBuilder::IdStringList RegionsBuilder::ToIdStringList(Node::Ptr tree) const
{
  IdStringList result;
  std::queue<Node::Ptr> queue;
  queue.push(tree);
  while (!queue.empty())
  {
    const auto el = queue.front();
    queue.pop();
    Node::PtrList nodes;
    auto current = el;
    while (current)
    {
      nodes.push_back(current);
      current = current->GetParent();
    }

    auto string = m_toStringPolicy->ToString(nodes);
    auto const id = nodes.front()->GetData().GetId();
    result.emplace_back(std::make_pair(id, std::move(string)));
    for (auto const & n : el->GetChildren())
      queue.push(n);
  }

  return result;
}

Node::PtrList RegionsBuilder::MakeSelectedRegionsByCountry(Region const & country,
                                                           Regions const & allRegions)
{
  Regions regionsInCountry;
  auto filterCopy = [&country] (const Region & r) { return country.ContainsRect(r); };
  std::copy_if(std::begin(allRegions), std::end(allRegions),
               std::back_inserter(regionsInCountry), filterCopy);

  regionsInCountry.emplace_back(country);
  auto const comp = [](const Region & l, const Region & r) {
    auto const lArea = l.GetArea();
    auto const rArea = r.GetArea();
    return lArea != rArea ? lArea > rArea : l.GetRank() < r.GetRank();
  };
  std::sort(std::begin(regionsInCountry), std::end(regionsInCountry), comp);

  Node::PtrList nodes;
  nodes.reserve(regionsInCountry.size());
  for (auto && region : regionsInCountry)
    nodes.emplace_back(std::make_shared<Node>(std::move(region)));

  return nodes;
}

Node::Ptr RegionsBuilder::BuildCountryRegionTree(Region const & country,
                                                 Regions const & allRegions)
{
  auto nodes = MakeSelectedRegionsByCountry(country, allRegions);
  while (nodes.size() > 1)
  {
    auto itFirstNode = std::rbegin(nodes);
    auto & firstRegion = (*itFirstNode)->GetData();
    auto itCurr = itFirstNode + 1;
    for (; itCurr != std::rend(nodes); ++itCurr)
    {
      auto const & currRegion = (*itCurr)->GetData();
      // todo(maksimandrianov1): It requires more research.
      // If Contains returns false, then we calculate the percent overlap of polygons.
      // We believe that if one polygon overlaps by 98 percent, then we can assume that one
      // contains another.
      // auto const kAvaliableOverlapPercentage = 98;
      if (currRegion.Contains(firstRegion) /* ||
           currRegion.CalculateOverlapPercentage(firstRegion) > kAvaliableOverlapPercentage*/ )
      {
        // In general, we assume that a region with the larger rank has the larger area.
        // But sometimes it does not. In this case, we will make an inversion.
        if (firstRegion.GetRank() < currRegion.GetRank())
        {
          (*itCurr)->SetParent(*itFirstNode);
          (*itFirstNode)->AddChild(*itCurr);
        }
        else
        {
          (*itFirstNode)->SetParent(*itCurr);
          (*itCurr)->AddChild(*itFirstNode);
        }
        // We want to free up memory.
        firstRegion.DeletePolygon();
        nodes.pop_back();
        break;
      }
    }

    if (itCurr == std::rend(nodes))
      nodes.pop_back();
  }

  return nodes.empty() ? std::shared_ptr<Node>() : ShrinkToFit(nodes.front());
}

void RegionsBuilder::ForEachNormalizedCountry(NormalizedCountryFn fn)
{
  for (auto const & countryName : GetCountryNames())
  {
    RegionsBuilder::Regions country;
    auto const & countries = GetCountries();
    auto const pred = [&](const Region & r) { return countryName == r.GetName(); };
    std::copy_if(std::begin(countries), std::end(countries), std::back_inserter(country), pred);
    auto const countryTrees = BuildCountryRegionTrees(country);
    auto mergedTree = std::accumulate(std::begin(countryTrees), std::end(countryTrees),
                                      Node::Ptr(), MergeTree);
    NormalizeTree(mergedTree);
    fn(countryName, mergedTree);
  }
}

std::vector<Node::Ptr> RegionsBuilder::BuildCountryRegionTrees(RegionsBuilder::Regions const & countries)
{
  std::vector<std::future<Node::Ptr>> tmp;
  {
    size_t const cpuCount = m_cpuCount > 0 ? static_cast<size_t>(m_cpuCount)
                                           : std::thread::hardware_concurrency();
    ASSERT_GREATER(cpuCount, 0, ());
    ThreadPool threadPool(cpuCount);
    for (auto const & country : countries)
    {
      auto result = threadPool.enqueue(&RegionsBuilder::BuildCountryRegionTree, country, m_regions);
      tmp.emplace_back(std::move(result));
    }
  }
  std::vector<Node::Ptr> res;
  res.reserve(tmp.size());
  std::transform(std::begin(tmp), std::end(tmp),
                 std::back_inserter(res), [](auto & f) { return f.get(); });
  return res;
}

void RegionsBuilder::MakeCountryTrees()
{
  m_countryTrees.clear();
  for (auto && tree : BuildCountryRegionTrees(GetCountries()))
    m_countryTrees.emplace(tree->GetData().GetName(), std::move(tree));
}

Node::Ptr RegionsBuilder::GetNormalizedCountryTree(std::string const & name)
{
  Node::Ptr mergedTree;
  CountryTrees const & countryTrees = GetCountryTrees();
  auto const keyRange = countryTrees.equal_range(name);
  using countryTreeItem = typename RegionsBuilder::CountryTrees::value_type;
  auto const binOp = [](Node::Ptr l, countryTreeItem r) { return MergeTree(l, r.second); };
  mergedTree = std::accumulate(keyRange.first, keyRange.second, Node::Ptr(), binOp);
  NormalizeTree(mergedTree);
  return mergedTree;
}
}  // namespace regions
}  // namespace generator

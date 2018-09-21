#include "generator/regions.hpp"

#include "generator/feature_builder.hpp"
#include "generator/generate_info.hpp"

#include "platform/platform.hpp"

#include "coding/file_name_utils.hpp"
#include "coding/transliteration.hpp"

#include "base/control_flow.hpp"
#include "base/macros.hpp"
#include "base/timer.hpp"

#include <chrono>
#include <fstream>
#include <functional>
#include <numeric>
#include <memory>
#include <queue>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "defines.hpp"

#include "3party/jansson/myjansson.hpp"
#include "3party/ThreadPool/ThreadPool.h"
#include "3party/boost/boost/math/special_functions/relative_difference.hpp"
#include "3party/boost/boost/range/adaptor/reversed.hpp"

namespace generator
{
namespace regions
{
namespace
{
using MergeFunc = std::function<Node::Ptr(Node::Ptr, Node::Ptr)>;
using PointCitiesMap = std::unordered_map<base::GeoObjectId, CityPoint>;

class JsonPolicy : public ToStringPolicyInterface
{
public:
  JsonPolicy(bool extendedOutput)
    : m_extendedOutput(extendedOutput)
  {
  }

  std::string ToString(Node::PtrList const & nodePtrList) override
  {
    auto const & main = nodePtrList.front()->GetData();
    auto const & country = nodePtrList.back()->GetData();

    auto geometry = my::NewJSONObject();
    ToJSONObject(*geometry, "type", "Point");
    auto coordinates = my::NewJSONArray();
    auto const center = main.GetCenter();
    ToJSONArray(*coordinates, center.get<0>());
    ToJSONArray(*coordinates, center.get<1>());
    ToJSONObject(*geometry, "coordinates", coordinates);

    auto localeEn = my::NewJSONObject();
    auto address = my::NewJSONObject();
    for (auto const & p : boost::adaptors::reverse(nodePtrList))
    {
      auto const & region = p->GetData();
      auto const label = region.GetLabel();
      ToJSONObject(*address, label, region.GetName());
      if (m_extendedOutput)
      {
        ToJSONObject(*address, label + "_i", region.GetId().GetSerialId());
        ToJSONObject(*address, label + "_a", region.GetArea());
        ToJSONObject(*address, label + "_r", region.GetRank());
      }

      ToJSONObject(*localeEn, label, region.GetEnglishOrTransliteratedName());
    }

    auto locales = my::NewJSONObject();
    ToJSONObject(*locales, "en", localeEn);

    auto properties = my::NewJSONObject();
    ToJSONObject(*properties, "name", main.GetName());
    ToJSONObject(*properties, "rank", main.GetRank());
    ToJSONObject(*properties, "address", address);
    ToJSONObject(*properties, "locales", locales);
    if (country.HasIsoCode())
      ToJSONObject(*properties, "code", country.GetIsoCode());

    auto feature = my::NewJSONObject();
    ToJSONObject(*feature, "type", "Feature");
    ToJSONObject(*feature, "geometry", geometry);
    ToJSONObject(*feature, "properties", properties);

    auto const cstr = json_dumps(feature.get(), JSON_COMPACT);
    std::unique_ptr<char, JSONFreeDeleter> buffer(cstr);
    return buffer.get();
  }

private:
  bool m_extendedOutput;
};

// This function is for debugging only and can be used for statistics collection.
size_t TreeSize(Node::Ptr node)
{
  if (node == nullptr)
    return 0;

  size_t size = 1;
  for (auto const & n : node->GetChildren())
    size += TreeSize(n);

  return size;
}

// This function is for debugging only and can be used for statistics collection.
size_t MaxDepth(Node::Ptr node)
{
  if (node == nullptr)
    return 0;

  size_t depth = 1;
  for (auto const & n : node->GetChildren())
    depth = std::max(MaxDepth(n), depth);

  return depth;
}

// This function is for debugging only and can be used for statistics collection.
void PrintTree(Node::Ptr node, std::ostream & stream = std::cout, std::string prefix = "",
               bool isTail = true)
{
  auto const & childern = node->GetChildren();
  stream << prefix;
  if (isTail)
  {
    stream << "└───";
    prefix += "    ";
  }
  else
  {
    stream << "├───";
    prefix += "│   ";
  }

  auto const & d = node->GetData();
  auto const point = d.GetCenter();
  stream << d.GetName() << "<" << d.GetEnglishOrTransliteratedName() << "> ("
         << d.GetId()
         << ";" << d.GetLabel()
         << ";" << static_cast<size_t>(d.GetRank())
         << ";[" << point.get<0>() << "," << point.get<1>() << "])"
         << std::endl;
  for (size_t i = 0, size = childern.size(); i < size; ++i)
    PrintTree(childern[i], stream, prefix, i == size - 1);
}

void DebugPrintCountry(Node::Ptr tree, std::ostream & stream = std::cout)
{
  stream << "COUNTRY: " << tree->GetData().GetName() << std::endl;
  stream << "MAX DEPTH: " <<  MaxDepth(tree) << std::endl;
  stream << "TREE SIZE: " <<  TreeSize(tree) << std::endl;
  PrintTree(tree, stream);
  stream << std::endl;
}

template <typename BoostGeometry, typename FbGeometry>
void FillBoostGeometry(BoostGeometry & geometry, FbGeometry const & fbGeometry)
{
  geometry.reserve(fbGeometry.size());
  for (auto const & p : fbGeometry)
    boost::geometry::append(geometry, BoostPoint{p.x, p.y});
}

std::tuple<RegionsBuilder::Regions, PointCitiesMap>
ReadDatasetFromTmpMwm(feature::GenerateInfo const & genInfo, RegionInfoCollector const & collector)
{
  RegionsBuilder::Regions regions;
  PointCitiesMap pointCitiesMap;
  auto const tmpMwmFilename = genInfo.GetTmpFileName(genInfo.m_fileName);
  auto const toDo = [&regions, &pointCitiesMap, &collector](FeatureBuilder1 const & fb, uint64_t /* currPos */)
  {
    if (fb.IsArea() && fb.IsGeometryClosed())
    {
      auto const id = fb.GetMostGenericOsmId();
      auto region = Region(fb, collector.Get(id));
      regions.emplace_back(std::move(region));
    }
    else if (fb.IsPoint())
    {
      auto const id = fb.GetMostGenericOsmId();
      pointCitiesMap.emplace(id, CityPoint(fb, collector.Get(id)));
    }
  };

  feature::ForEachFromDatRawFormat(tmpMwmFilename, toDo);
  return std::make_tuple(regions, pointCitiesMap);
}

void FixRegions(RegionsBuilder::Regions & regions, PointCitiesMap const & pointCitiesMap)
{
  RegionsBuilder::Regions regionsWithAdminCenter;
  auto const pred = [](Region const & region) { return region.GetAdminCenterId().IsValid(); };
  std::copy_if(std::begin(regions), std::end(regions), std::back_inserter(regionsWithAdminCenter), pred);
  auto const it =  std::remove_if(std::begin(regions), std::end(regions), pred);
  regions.erase(it, std::end(regions));

  std::multimap<std::string, std::reference_wrapper<Region const>> m;
  for (auto const & region : regions)
  {
    auto const name = region.GetName();
    if (region.GetLabel() == "locality" && !name.empty())
      m.emplace(name, region);
  }

  auto const cmp = [](Region const & l, Region const & r) { return l.GetArea() < r.GetArea(); };
  std::sort(std::begin(regionsWithAdminCenter), std::end(regionsWithAdminCenter), cmp);
  std::vector<bool> unsuitable;
  unsuitable.resize(regionsWithAdminCenter.size());
  for (size_t i = 0; i < regionsWithAdminCenter.size(); ++i)
  {
    if (unsuitable[i])
      continue;

    auto & regionWithAdminCenter = regionsWithAdminCenter[i];
    if (regionWithAdminCenter.IsCountry())
    {
      unsuitable[i] = true;
      continue;
    }

    for (size_t j =  i + 1; j < regionsWithAdminCenter.size() - 1; ++j)
    {
      if (regionsWithAdminCenter[j].ContainsRect(regionWithAdminCenter))
        unsuitable[j] = true;
    }

    auto const id = regionWithAdminCenter.GetAdminCenterId();
    if (!pointCitiesMap.count(id))
    {
      unsuitable[i] = true;
      continue;
    }

    auto const & adminCenter = pointCitiesMap.at(id);
    auto const range = m.equal_range(adminCenter.GetName());
    for (auto it = range.first; it != range.second; ++it)
    {
      Region const & r = it->second;
      if (adminCenter.GetRank() == r.GetRank() && r.Contains(adminCenter))
      {
        unsuitable[i] = true;
        break;
      }
    }

    if (!unsuitable[i])
      regionWithAdminCenter.SetInfo(adminCenter);
  }

  std::move(std::begin(regionsWithAdminCenter), std::end(regionsWithAdminCenter),
            std::back_inserter(regions));
}

void FilterRegions(RegionsBuilder::Regions & regions)
{
  auto const pred = [](Region const & region)
  {
    auto const & label = region.GetLabel();
    auto const & name = region.GetName();
    return label.empty() || name.empty();
  };
  auto const it = std::remove_if(std::begin(regions), std::end(regions), pred);
  regions.erase(it, std::end(regions));
}

bool LessNodePtrById(Node::Ptr l, Node::Ptr r)
{
  auto const & lRegion = l->GetData();
  auto const & rRegion = r->GetData();
  return lRegion.GetId() < rRegion.GetId();
}

Node::PtrList MergeChildren(Node::PtrList const & l, Node::PtrList const & r, Node::Ptr newParent)
{
  Node::PtrList result(l);
  std::copy(std::begin(r), std::end(r), std::back_inserter(result));
  for (auto & p : result)
    p->SetParent(newParent);

  std::sort(std::begin(result), std::end(result), LessNodePtrById);
  return result;
}

Node::PtrList NormalizeChildren(Node::PtrList const & children, MergeFunc mergeTree)
{
  Node::PtrList uniqueChildren;
  auto const pred = [](Node::Ptr l, Node::Ptr r)
  {
    auto const & lRegion = l->GetData();
    auto const & rRegion = r->GetData();
    return lRegion.GetId() == rRegion.GetId();
  };
  std::unique_copy(std::begin(children), std::end(children),
                   std::back_inserter(uniqueChildren), pred);
  Node::PtrList result;
  for (auto const & ch : uniqueChildren)
  {
    auto const bounds = std::equal_range(std::begin(children), std::end(children),
                                         ch, LessNodePtrById);
    auto merged = std::accumulate(bounds.first, bounds.second, Node::Ptr(), mergeTree);
    result.emplace_back(std::move(merged));
  }

  return result;
}

Node::Ptr MergeHelper(Node::Ptr l, Node::Ptr r, MergeFunc mergeTree)
{
  auto const & lChildren = l->GetChildren();
  auto const & rChildren = r->GetChildren();
  auto const children = MergeChildren(lChildren, rChildren, l);
  if (children.empty())
    return l;

  auto resultChildren = NormalizeChildren(children, mergeTree);
  l->SetChildren(std::move(resultChildren));
  r->RemoveChildren();
  return l;
}

// This function merges two trees if the roots have the same name.
Node::Ptr MergeTree(Node::Ptr l, Node::Ptr r)
{
  if (l == nullptr)
    return r;

  if (r == nullptr)
    return l;

  auto const & lRegion = l->GetData();
  auto const & rRegion = r->GetData();
  if (lRegion.GetId() != rRegion.GetId())
    return nullptr;

  if (lRegion.GetArea() > rRegion.GetArea())
    return MergeHelper(l, r, MergeTree);
  else
    return MergeHelper(r, l, MergeTree);
}

// This function corrects the tree. It traverses the whole node and unites children with
// the same names.
void NormalizeTree(Node::Ptr tree)
{
  if (tree == nullptr)
    return;

  auto & children = tree->GetChildren();
  std::sort(std::begin(children), std::end(children), LessNodePtrById);
  auto newChildren = NormalizeChildren(children, MergeTree);
  tree->SetChildren(std::move(newChildren));
  for (auto const & ch : tree->GetChildren())
    NormalizeTree(ch);
}
}  // namespace

std::string StringUtf8MultilangNamable::GetName(int8_t lang) const
{
  std::string s;
  VERIFY(m_name.GetString(lang, s) != s.empty(), ());
  return s;
}

std::string StringUtf8MultilangNamable::GetEnglishOrTransliteratedName() const
{
  std::string s = GetName(StringUtf8Multilang::kEnglishCode);
  if (!s.empty())
    return s;

  auto const fn = [&s, this](int8_t code, std::string const & name)
  {
    if (code != StringUtf8Multilang::kDefaultCode &&
        Transliteration::Instance().Transliterate(name, code, s))
    {
      return base::ControlFlow::Break;
    }

    return base::ControlFlow::Continue;
  };

  m_name.ForEach(fn);
  return s;
}

StringUtf8Multilang const & StringUtf8MultilangNamable::GetStringUtf8MultilangName() const
{
  return m_name;
}

void StringUtf8MultilangNamable::SetStringUtf8MultilangName(StringUtf8Multilang const & name)
{
  m_name = name;
}

base::GeoObjectId RegionDatable::GetId() const
{
  return m_regionData.GetOsmId();
}

bool RegionDatable::HasAdminCenter() const
{
  return m_regionData.HasAdminCenter();
}

base::GeoObjectId RegionDatable::GetAdminCenterId() const
{
  return m_regionData.GetAdminCenter();
}

bool RegionDatable::HasIsoCode() const
{
  return m_regionData.HasIsoCodeAlpha2();
}

std::string RegionDatable::GetIsoCode() const
{
  return m_regionData.GetIsoCodeAlpha2();
}

// The values ​​of the administrative level and place are indirectly dependent.
// This is used when calculating the rank.
uint8_t RegionDatable::GetRank() const
{
  auto const adminLevel = m_regionData.GetAdminLevel();
  auto const placeType = m_regionData.GetPlaceType();

  switch (placeType)
  {
  case PlaceType::City:
  case PlaceType::Town:
  case PlaceType::Village:
  case PlaceType::Hamlet:
  case PlaceType::Suburb:
  case PlaceType::Neighbourhood:
  case PlaceType::Locality:
  case PlaceType::IsolatedDwelling: return static_cast<uint8_t>(placeType);
  default: break;
  }

  switch (adminLevel)
  {
  case AdminLevel::Two:
  case AdminLevel::Four:
  case AdminLevel::Six: return static_cast<uint8_t>(adminLevel);
  default: break;
  }

  return kNoRank;
}

std::string RegionDatable::GetLabel() const
{
  auto const adminLevel = m_regionData.GetAdminLevel();
  auto const placeType = m_regionData.GetPlaceType();

  switch (placeType)
  {
  case PlaceType::City:
  case PlaceType::Town:
  case PlaceType::Village:
  case PlaceType::Hamlet: return "locality";
  case PlaceType::Suburb:
  case PlaceType::Neighbourhood: return "suburb";
  case PlaceType::Locality:
  case PlaceType::IsolatedDwelling: return "sublocality";
  default: break;
  }

  switch (adminLevel)
  {
  case AdminLevel::Two: return "country";
  case AdminLevel::Four: return "region";
  case AdminLevel::Six: return "subregion";
  default: break;
  }

  return "";
}

CityPoint::CityPoint(FeatureBuilder1 const & fb, RegionDataProxy const & rd)
  : StringUtf8MultilangNamable(fb.GetParams().name),
    RegionDatable(rd)
{
  auto const p = fb.GetKeyPoint();
  m_center = {p.x, p.y};
}

BoostPoint CityPoint::GetCenter() const
{
  return m_center;
}

Region::Region(FeatureBuilder1 const & fb, RegionDataProxy const & rd)
  : StringUtf8MultilangNamable(fb.GetParams().name),
    RegionDatable(rd),
    m_polygon(std::make_shared<BoostPolygon>())
{
  FillPolygon(fb);
  auto rect = fb.GetLimitRect();
  m_rect = BoostRect({{rect.minX(), rect.minY()}, {rect.maxX(), rect.maxY()}});
  m_area = boost::geometry::area(*m_polygon);
}

void Region::DeletePolygon()
{
  m_polygon = nullptr;
}

void Region::FillPolygon(FeatureBuilder1 const & fb)
{
  CHECK(m_polygon, ());

  auto const & fbGeometry = fb.GetGeometry();
  CHECK(!fbGeometry.empty(), ());
  auto it = std::begin(fbGeometry);
  FillBoostGeometry(m_polygon->outer(), *it);
  m_polygon->inners().resize(fbGeometry.size() - 1);
  int i = 0;
  ++it;
  for (; it != std::end(fbGeometry); ++it)
    FillBoostGeometry(m_polygon->inners()[i++], *it);

  boost::geometry::correct(*m_polygon);
}

bool Region::IsCountry() const
{
  static auto const kAdminLevelCountry = AdminLevel::Two;
  return !HasAdminLevel() && m_regionData.GetAdminLevel() == kAdminLevelCountry;
}

bool Region::Contains(Region const & smaller) const
{
  CHECK(m_polygon, ());
  CHECK(smaller.m_polygon, ());

  return boost::geometry::covered_by(*smaller.m_polygon, *m_polygon);
}

double Region::CalculateOverlapPercentage(Region const & other) const
{
  CHECK(m_polygon, ());
  CHECK(other.m_polygon, ());

  std::vector<BoostPolygon> coll;
  boost::geometry::intersection(*other.m_polygon, *m_polygon, coll);
  auto const min = std::min(boost::geometry::area(*other.m_polygon),
                            boost::geometry::area(*m_polygon));
  auto const binOp = [] (double x, BoostPolygon const & y) { return x + boost::geometry::area(y); };
  auto const sum = std::accumulate(std::begin(coll), std::end(coll), 0., binOp);
  return (sum / min) * 100;
}

bool Region::ContainsRect(Region const & smaller) const
{
  return boost::geometry::covered_by(smaller.m_rect, m_rect);
}

BoostPoint Region::GetCenter() const
{
  BoostPoint p;
  boost::geometry::centroid(m_rect, p);
  return p;
}

BoostRect const & Region::GetRect() const
{
  return m_rect;
}

std::shared_ptr<BoostPolygon> const Region::GetPolygon() const
{
  return m_polygon;
}

double Region::GetArea() const
{
  return m_area;
}

bool Region::Contains(CityPoint const & cityPoint) const
{
  CHECK(m_polygon, ());

  return boost::geometry::covered_by(cityPoint.GetCenter(), *m_polygon);
}

void Region::SetInfo(CityPoint const & cityPoint)
{
  SetStringUtf8MultilangName(cityPoint.GetStringUtf8MultilangName());
  SetAdminLevel(cityPoint.GetAdminLevel());
  SetPlaceType(cityPoint.GetPlaceType());
}

RegionsBuilder::RegionsBuilder(Regions && regions,
                               std::unique_ptr<ToStringPolicyInterface> toStringPolicy)
  : m_toStringPolicy(std::move(toStringPolicy))
{
  ASSERT(m_toStringPolicy, ());

  auto const isCountry = [](Region const & r){ return r.IsCountry(); };
  std::copy_if(std::begin(regions), std::end(regions), std::back_inserter(m_countries), isCountry);
  auto const it = std::remove_if(std::begin(regions), std::end(regions), isCountry);
  regions.erase(it, std::end(regions));
  auto const cmp = [](Region const & l, Region const & r) { return l.GetArea() > r.GetArea(); };
  std::sort(std::begin(m_countries), std::end(m_countries), cmp);

  MakeCountryTrees(regions);
}

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
  auto const comp = [](const Region & l, const Region & r)
  {
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
      // If Contains returns false, then we calculate the percent overlap of polygons.
      // We believe that if one polygon overlaps by 98 percent, then we can assume that one
      // contains another.
      auto const kAvaliableOverlapPercentage = 98;
      if (currRegion.ContainsRect(firstRegion) &&
          (currRegion.Contains(firstRegion) ||
           currRegion.CalculateOverlapPercentage(firstRegion) > kAvaliableOverlapPercentage))
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

  return nodes.empty() ? std::shared_ptr<Node>() : nodes.front();
}

void RegionsBuilder::MakeCountryTrees(Regions const & regions)
{
  std::vector<std::future<Node::Ptr>> results;
  {
    auto const cpuCount = 1; //std::thread::hardware_concurrency();
    ASSERT_GREATER(cpuCount, 0, ());
    ThreadPool threadPool(cpuCount);
    for (auto const & country : GetCountries())
    {
      auto f = threadPool.enqueue(&RegionsBuilder::BuildCountryRegionTree, country, regions);
      results.emplace_back(std::move(f));
    }
  }

  for (auto & r : results)
  {
    auto tree = r.get();
    m_countryTrees.emplace(tree->GetData().GetName(), std::move(tree));
  }
}
}  // namespace regions

bool GenerateRegions(feature::GenerateInfo const & genInfo)
{
  using namespace regions;

  LOG(LINFO, ("Start generating regions.."));
  auto timer = my::Timer();

  Transliteration::Instance().Init(GetPlatform().ResourcesDir());

  auto const collectorFilename =
      genInfo.GetTmpFileName(genInfo.m_fileName, RegionInfoCollector::kDefaultExt);
  RegionInfoCollector regionsInfoCollector(collectorFilename);

  RegionsBuilder::Regions regions;
  PointCitiesMap pointCitiesMap;
  std::tie(regions, pointCitiesMap) = ReadDatasetFromTmpMwm(genInfo, regionsInfoCollector);

  FixRegions(regions, pointCitiesMap);
  FilterRegions(regions);

  auto jsonPolicy = std::make_unique<JsonPolicy>(genInfo.m_verbose);
  auto kvBuilder = std::make_unique<RegionsBuilder>(std::move(regions), std::move(jsonPolicy));
  auto const countryTrees = kvBuilder->GetCountryTrees();

  auto const jsonlName = genInfo.GetIntermediateFileName(genInfo.m_fileName, ".jsonl");
  std::ofstream ofs(jsonlName, std::ofstream::out);
  std::set<base::GeoObjectId> setIds;
  size_t countIds = 0;
  for (auto const & countryName : kvBuilder->GetCountryNames())
  {
    auto const keyRange = countryTrees.equal_range(countryName);
    using countryTreeItem = typename RegionsBuilder::CountryTrees::value_type;
    auto const binOp = [](Node::Ptr l, countryTreeItem r) { return MergeTree(l, r.second); };
    Node::Ptr mergedTree = std::accumulate(keyRange.first, keyRange.second, Node::Ptr(), binOp);
    if (!mergedTree)
      continue;

    NormalizeTree(mergedTree);
    if (genInfo.m_verbose)
      DebugPrintCountry(mergedTree);

    auto const idStringList = kvBuilder->ToIdStringList(mergedTree);
    for (auto const & s : idStringList)
    {
      ofs << s.first << " " << s.second << std::endl;
      ++countIds;
      if (!setIds.insert(s.first).second)
        LOG(LWARNING, ("Id alredy exists:",  s.first));
    }
  }

  LOG(LINFO, (countIds, "total ids.", setIds.size(), "unique ids."));
  LOG(LINFO, ("Finish generating regions.", timer.ElapsedSeconds(), "seconds."));
  return true;
}
}  // namespace generator

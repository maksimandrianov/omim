#include "generator/hierarchy.hpp"

#include "generator/boost_helpers.hpp"

#include "indexer/classificator.hpp"
#include "indexer/feature_utils.hpp"

#include "geometry/mercator.hpp"
#include "geometry/rect2d.hpp"

#include "base/string_utils.hpp"
#include "base/stl_helpers.hpp"

#include <algorithm>
#include <functional>
#include <fstream>
#include <limits>
#include <list>
#include <map>
#include <memory>

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/register/point.hpp>
#include <boost/geometry/multi/geometries/register/multi_point.hpp>

namespace generator
{
namespace hierarchy
{
namespace
{
bool Less(FeatureBuilder1 const & l, FeatureBuilder1 const & r)
{
  if (l.IsPoint() && r.IsArea())
    return true;

  if (l.GetTypesCount() < r.GetTypesCount())
    return true;

  if (l.GetMetadata().Size() < r.GetMetadata().Size())
    return true;

  return false;
}
}  // namespace

HierarchyGeomPlace::HierarchyGeomPlace(FeatureBuilder1 const & feature)
  : m_id(feature.GetMostGenericOsmId())
  , m_polygon(std::make_unique<BoostPolygon>())
  , m_name(GetFeatureName(feature))
  , m_types(feature.GetTypes())
{
  CHECK(m_polygon, ());
  boost_helpers::FillPolygon(*m_polygon, feature, false /* fillHoles */);
  boost::geometry::envelope(*m_polygon, m_rect);
  m_area = boost::geometry::area(*m_polygon);
  m_center = feature.GetKeyPoint();
}

HierarchyGeomPlace::HierarchyGeomPlace(std::vector<FeatureBuilder1> const & features)
  : m_polygon(std::make_unique<BoostPolygon>())
{
  CHECK(m_polygon, ());
  CHECK(!features.empty(), ());

  m_id = features.front().GetMostGenericOsmId();
  m_name = GetFeatureName(features.front());
  m_types = features.front().GetTypes();

  boost::geometry::model::multi_point<BoostPoint> points;
  for (auto const & feature : features)
  {
    auto const & outer = feature.GetOuterGeometry();
    std::transform(std::begin(outer), std::end(outer), std::back_inserter(points), [] (auto const & p) {
      return BoostPoint{p.x, p.y};
    });
  }

  boost::geometry::convex_hull(points, *m_polygon);
  boost::geometry::envelope(*m_polygon, m_rect);
  m_area = boost::geometry::area(*m_polygon);
  BoostPoint p;
  boost::geometry::centroid(*m_polygon, p);
  m_center = {p.get<0>(), p.get<1>()};
}

bool HierarchyGeomPlace::Contains(HierarchyGeomPlace const & smaller) const
{
  CHECK(m_polygon, ());
  CHECK(smaller.m_polygon, ());

  return boost::geometry::covered_by(*smaller.m_polygon, *m_polygon);
}

bool HierarchyGeomPlace::Contains(m2::PointD const & point) const
{
  CHECK(m_polygon, ());

  return boost::geometry::covered_by(BoostPoint(point.x, point.y), *m_polygon);
}

// static
uint8_t HierarchyBuilder::GetLevel(Node::Ptr node)
{
  auto const tmp = node;
  uint8_t level = 0;
  while (node)
  {
    node = node->GetParent();
    ++level;
  }

  return level;
}

// static
boost::optional<base::GeoObjectId>
HierarchyBuilder::FindPointParent(m2::PointD const & point, MapIdToNode const & m, Tree4d const & tree)
{
  boost::optional<base::GeoObjectId> bestId;
  auto minArea = std::numeric_limits<double>::max();
  base::GeoObjectId minId;
  tree.ForEachInRect({point, point}, [&](base::GeoObjectId const & id) {
    if (m.count(id) == 0)
      return;

    auto const & r = m.at(id)->GetData();
    if ((r.GetArea() == minArea ? minId < r.GetId() : r.GetArea() < minArea) && r.Contains(point))
    {
      minArea = r.GetArea();
      minId = r.GetId();
      bestId = id;
    }
  });

  return bestId;
}

// static
boost::optional<HierarchyBuilder::Node::Ptr>
HierarchyBuilder::FindPopularityGeomPlaceParent(HierarchyGeomPlace const & place,
                                                MapIdToNode const & m, Tree4d const & tree)
{
  boost::optional<Node::Ptr> bestPlace;
  auto minArea = std::numeric_limits<double>::max();
  auto const point = place.GetCenter();
  tree.ForEachInRect({point, point}, [&](base::GeoObjectId const & id) {
    if (m.count(id) == 0)
      return;

    auto const & r = m.at(id)->GetData();
    if (r.GetId() == place.GetId() || r.GetArea() < place.GetArea())
      return;

    if ((r.GetArea() == place.GetArea() ? place.GetId() < r.GetId() : r.GetArea() < minArea) &&
        r.Contains(place))
    {
      minArea = r.GetArea();
      bestPlace = m.at(id);
    }
  });

  return bestPlace;
}

// static
HierarchyBuilder::MapIdToNode HierarchyBuilder::GetAreaMap(Node::PtrList const & nodes)
{
  std::unordered_map<base::GeoObjectId, Node::Ptr> result;
  result.reserve(nodes.size());
  for (auto const & n : nodes)
  {
    auto const & d = n->GetData();
    result.emplace(d.GetId(), n);
  }

  return result;
}

// static
HierarchyBuilder::Tree4d HierarchyBuilder::MakeTree4d(Node::PtrList const & nodes)
{
  Tree4d tree;
  for (auto const & n : nodes)
  {
    auto const & data = n->GetData();
    tree.Add(data.GetId(), data.GetLimitRect());
  }

  return tree;
}

// static
void HierarchyBuilder::LinkGeomPlaces(MapIdToNode const & m, Tree4d const & tree, Node::PtrList & nodes)
{
  if (nodes.size() < 2)
    return;

  std::sort(std::begin(nodes), std::end(nodes), [](Node::Ptr const & l, Node::Ptr const & r) {
    return l->GetData().GetArea() < r->GetData().GetArea();
  });

  for (auto & node : nodes)
  {
    auto const & place = node->GetData();
    auto const parentPlace = FindPopularityGeomPlaceParent(place, m, tree);
    if (!parentPlace)
      continue;

    (*parentPlace)->AddChild(node);
    node->SetParent(*parentPlace);
  }
}

// static
HierarchyBuilder::Node::PtrList
HierarchyBuilder::MakeNodes(std::vector<FeatureBuilder1> const & features)
{
  Node::PtrList nodes;
  nodes.reserve(features.size());

  std::multimap<base::GeoObjectId, FeatureBuilder1> m;
  for (auto const & feature : features)
    m.emplace(feature.GetMostGenericOsmId(), feature);

  for(auto it = std::begin(m), end = std::end(m); it != end; it = m.upper_bound(it->first))
  {
    auto const iters = m.equal_range(it->first);
    if (std::distance(iters.first, iters.second) == 1)
    {
      nodes.push_back(std::make_shared<Node>(HierarchyGeomPlace(iters.first->second)));
      continue;
    }

    std::vector<FeatureBuilder1> temp;
    std::transform(iters.first, iters.second, std::back_inserter(temp), [](auto const & p) {
      return p.second;
    });
    nodes.push_back(std::make_shared<Node>(HierarchyGeomPlace(temp)));

  }

  return nodes;
}

HierarchyBuilder::HierarchyBuilder(std::string const & dataFilename, ftypes::BaseChecker const & checker)
  : m_dataFullFilename(dataFilename)
  , m_dataFilename(base::GetNameFromFullPathWithoutExt(dataFilename))
  , m_checker(checker) {}

std::string HierarchyBuilder::GetType(FeatureParams::Types const & types) const
{
  auto const & c = classif();
  auto const it = std::find_if(std::cbegin(types), std::cend(types), std::cref(m_checker));
  return it == std::cend(types) ? string() : c.GetReadableObjectName(*it);
}

void HierarchyBuilder::FillLinesFromPointObjects(std::vector<FeatureBuilder1> const & pointObjs,
                                                 MapIdToNode const & m, Tree4d const & tree,
                                                 std::vector<HierarchyLine> & lines) const
{
  lines.reserve(lines.size() + pointObjs.size());
  for (auto const & p : pointObjs)
  {
    auto const center = p.GetKeyPoint();
    HierarchyLine line;
    line.m_id = p.GetMostGenericOsmId();
    line.m_parent = FindPointParent(center, m, tree);
    line.m_center = center;
    line.m_type = GetType(p.GetTypes());
    line.m_name = GetFeatureName(p);
    line.m_dataFilename = m_dataFilename;
    line.m_level = line.m_parent ? GetLevel(m.at(*line.m_parent)) + 1 : 0;
    line.m_hasChildren = false;
    lines.push_back(line);
  }
}

void HierarchyBuilder::FillLineFromGeomObjectPtr(HierarchyLine & line, Node::Ptr const & node) const
{
  auto const & data = node->GetData();
  line.m_id = data.GetId();
  if (node->HasParent())
    line.m_parent = node->GetParent()->GetData().GetId();

  line.m_center = data.GetCenter();
  line.m_type = GetType(data.GetTypes());
  line.m_name = data.GetName();
  line.m_dataFilename = m_dataFilename;
  line.m_level = GetLevel(node);
  line.m_hasChildren = node->HasChildren();
}

void HierarchyBuilder::FillLinesFromGeomObjectPtrs(Node::PtrList const & nodes,
                                                   std::vector<HierarchyLine> & lines) const
{
  lines.reserve(lines.size() + nodes.size());
  for (auto const & n : nodes)
  {
    HierarchyLine line;
    FillLineFromGeomObjectPtr(line, n);
    lines.push_back(line);
  }
}

void HierarchyBuilder::SetDataFilename(std::string const & dataFilename)
{
  m_dataFullFilename = dataFilename;
}

void HierarchyBuilder::ReadFeatures(std::string const & dataFilename, std::vector<FeatureBuilder1> & features) const
{
  feature::ForEachFromDatRawFormat(dataFilename, [&](FeatureBuilder1 const & fb, uint64_t /* currPos */) {
    if (!m_checker(fb.GetTypesHolder()) || GetFeatureName(fb).empty() || fb.GetOsmIds().empty())
      return;

    if (fb.IsPoint() || fb.IsArea())
      features.push_back(fb);
  });
}

// static
void HierarchyBuilder::DivideFeatures(std::vector<FeatureBuilder1> const & features, std::vector<FeatureBuilder1> & pointObjs,
                                      std::vector<FeatureBuilder1> & geomObjs)
{
  std::copy_if(std::begin(features), std::end(features), std::back_inserter(pointObjs), [] (auto const f) {
    return f.IsPoint();
  });

  std::copy_if(std::begin(features), std::end(features), std::back_inserter(geomObjs), [] (auto const f) {
    return f.IsArea();
  });
}

void HierarchyBuilder::Prepare(std::vector<FeatureBuilder1> & geomObjs, Node::PtrList & geomObjsPtrs,
                               Tree4d & tree, MapIdToNode & mapIdToNode) const
{
  geomObjsPtrs = MakeNodes(geomObjs);
  tree = MakeTree4d(geomObjsPtrs);
  mapIdToNode = GetAreaMap(geomObjsPtrs);
  LinkGeomPlaces(mapIdToNode, tree, geomObjsPtrs);
}

std::string GetFeatureName(FeatureBuilder1 const & feature)
{
  auto const & str = feature.GetParams().name;
  auto const deviceLang = StringUtf8Multilang::GetLangIndex("ru");
  std::string result;
  feature::GetReadableName({}, str, deviceLang, false /* allowTranslit */, result);
  std::replace(std::begin(result), std::end(result), ';', ',');
  std::replace(std::begin(result), std::end(result), '\n', ',');
  return result;
}

void RemoveDuplicate(std::vector<FeatureBuilder1> & features,
                     std::function<std::string(FeatureParams::Types const & types)> getType)
{
  auto const kWordDifferencePercent = 10.0;
  auto const kRectSizeMeters = 50;

  m4::Tree<FeatureBuilder1> tree;
  for (auto const & feature : features)
    tree.Add(feature, feature.GetLimitRect());

  std::set<base::GeoObjectId> removingSet;
  for (auto const & feature : features)
  {
    auto const id = feature.GetMostGenericOsmId();
    if (removingSet.count(id) != 0)
      continue;

    auto const name = GetFeatureName(feature);
    auto const type = getType(feature.GetTypes());
    auto const dist =  static_cast<size_t>(std::ceil(static_cast<double>(name.size()) * kWordDifferencePercent / 100.0));
    std::vector<FeatureBuilder1> sameObjects;
    tree.ForEachInRect(MercatorBounds::RectByCenterXYAndSizeInMeters(feature.GetKeyPoint(), kRectSizeMeters), [&](auto const & ft) {
      auto const ftName = GetFeatureName(ft);
      auto const ftType = getType(ft.GetTypes());
      if (id.GetType() != base::GeoObjectId::Type::ObsoleteOsmNode ||
          type != ftType || strings::EditDistance(name, ftName) > dist)
        return;

      sameObjects.push_back(ft);
      // std::cout << DebugPrint(id) << "," << name << "," << type  << " - " << DebugPrint(ftId)  << "," << ftName << "," << ftType  <<  " - " << MercatorBounds::DistanceOnEarth(feature.GetKeyPoint(), ft.GetKeyPoint()) << std::endl;
    });

    if (sameObjects.size() > 1)
    {
      std::sort(std::begin(sameObjects), std::end(sameObjects), Less);
      std::transform(std::begin(sameObjects), std::end(sameObjects) - 1,
                     std::inserter(removingSet, std::end(removingSet)), [](auto const & feature) {
        return feature.GetMostGenericOsmId();
      });
    }
  }

  LOG(LINFO, ("Removed", removingSet.size(), "elements"));
  base::EraseIf(features, [&](auto const & feature) {
    return removingSet.count(feature.GetMostGenericOsmId()) != 0;
  });

  //  std::unordered_map<std::pair<base::GeoObjectId, base::GeoObjectId>, double> cache;
}
}  // namespace hierarchy

namespace popularity
{
using namespace hierarchy;

Builder::Builder(std::string const & dataFilename) :
  HierarchyBuilder(dataFilename, ftypes::IsPopularityPlaceChecker::Instance()) {}

std::vector<HierarchyLine> Builder::Build() const
{
  std::vector<FeatureBuilder1> features;
  ReadFeatures(m_dataFullFilename, features);
  auto callable = std::bind(&Builder::GetType, this, std::placeholders::_1);
  RemoveDuplicate(features, callable);

  std::vector<FeatureBuilder1> pointObjs;
  std::vector<FeatureBuilder1> geomObjs;
  DivideFeatures(features, pointObjs, geomObjs);

  Node::PtrList geomObjsPtrs;
  Tree4d tree;
  MapIdToNode mapIdToNode;
  Prepare(geomObjs, geomObjsPtrs, tree, mapIdToNode);

  std::vector<HierarchyLine> result;
  FillLinesFromGeomObjectPtrs(geomObjsPtrs, result);
  FillLinesFromPointObjects(pointObjs, mapIdToNode, tree, result);
  return result;
}


// static
void Writer::WriteLines(std::vector<hierarchy::HierarchyLine> const & lines,
                        std::string const & outFilename)
{
  std::ofstream stream;
  stream.exceptions(std::fstream::failbit | std::fstream::badbit);
  stream.open(outFilename);
  stream << std::fixed << std::setprecision(7);
  stream << "Id;Parent id;Lat;Lon;Main type;Name\n";
  for (auto const & line : lines)
  {
    stream << line.m_id.GetEncodedId() << ";";
    if (line.m_parent)
      stream << *line.m_parent;

    auto const center = MercatorBounds::ToLatLon(line.m_center);
    stream << ";" << center.lat << ";" << center.lon << ";"
           << line.m_type << ";" << line.m_name << "\n";
  }
}
}  // namespace popularity

namespace complex_area
{
using namespace hierarchy;

Builder::Builder(std::string const & dataFilename) :
  HierarchyBuilder(dataFilename, ftypes::IsComplexChecker::Instance()) {}

std::string Builder::GetType(FeatureParams::Types const & types) const
{
  auto const & c = classif();
  auto it = std::find_if(std::cbegin(types), std::cend(types), std::cref(ftypes::IsPoiChecker::Instance()));
  if (it != std::cend(types))
    return c.GetReadableObjectName(*it);

  it = std::find_if(std::cbegin(types), std::cend(types), [&](auto const & t) {
    return m_checker(t) && c.GetReadableObjectName(t) != "building";
  });

  return it == std::cend(types) ? string() : c.GetReadableObjectName(*it);
}

std::vector<HierarchyLine> Builder::Build() const
{
  std::vector<FeatureBuilder1> features;
  ReadFeatures(m_dataFullFilename, features);

  std::vector<FeatureBuilder1> pointObjs;
  std::vector<FeatureBuilder1> geomObjs;
  DivideFeatures(features, pointObjs, geomObjs);

  Node::PtrList geomObjsPtrs;
  Tree4d tree;
  MapIdToNode mapIdToNode;
  Prepare(geomObjs, geomObjsPtrs, tree, mapIdToNode);

  std::vector<HierarchyLine> result;
  FillLinesFromGeomObjectPtrs(geomObjsPtrs, result);

  std::vector<HierarchyLine> tmp;
  FillLinesFromPointObjects(pointObjs, mapIdToNode, tree, tmp);
  std::copy_if(std::cbegin(tmp), std::cend(tmp), std::back_inserter(result), [] (auto & p) {
    return static_cast<bool>(p.m_parent);
  });
  return result;
}

// static
void Writer::WriteLines(std::vector<hierarchy::HierarchyLine> const & lines,
                        std::string const & outFilename)
{
  std::ofstream stream;
  stream.exceptions(std::fstream::failbit | std::fstream::badbit);
  stream.open(outFilename);
  stream << std::fixed << std::setprecision(7);
  std::map<uint8_t, size_t> statistic;
  stream << "Id;Parent id;Lat;Lon;Main type;Name;MwmName;Level\n";
  for (auto const & line : lines)
  {
    stream << line.m_id.GetEncodedId() << ";";
    if (line.m_parent)
      stream << *line.m_parent;

    auto const center = MercatorBounds::ToLatLon(line.m_center);
    stream << ";" << center.lat << ";" << center.lon << ";"
           << line.m_type << ";" << line.m_name << ";"
           << line.m_dataFilename << ";" << static_cast<int>(line.m_level) <<"\n";

    statistic[line.m_level] += 1;
  }

  LOG(LINFO, ("Сompleted building file", outFilename));
  for (auto const & item : statistic)
    LOG(LINFO, (item.second, "elements on the level", static_cast<size_t>(item.first)));
}
}  // namespace complex_area
}  // namespace generator

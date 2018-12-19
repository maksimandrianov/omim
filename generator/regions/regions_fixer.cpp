#include "generator/regions/regions_fixer.hpp"

#include "base/geo_object_id.hpp"
#include "base/logging.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <boost/optional.hpp>

namespace generator
{
namespace regions
{
namespace
{
class RegionLocalityChecker
{
public:
  RegionLocalityChecker() = default;
  RegionLocalityChecker(RegionsBuilder::Regions const & regions)
  {
    for (auto const & region : regions)
    {
      auto const name = region.GetName();
      if (region.IsLocality() && !name.empty())
        m_nameRegionMap.emplace(name, region);
    }
  }

  bool CityExistsAsRegion(City const & city)
  {
    auto const range = m_nameRegionMap.equal_range(city.GetName());
    for (auto it = range.first; it != range.second; ++it)
    {
      Region const & r = it->second;
      if (city.GetRank() == r.GetRank() && r.Contains(city))
        return true;
    }

    return false;
  }

private:
  std::multimap<std::string, std::reference_wrapper<Region const>> m_nameRegionMap;
};

class RegionsFixerWithAdminCenter
{
public:
  RegionsFixerWithAdminCenter(RegionsBuilder::Regions && regions, PointCitiesMap const & pointCitiesMap)
    : m_regions(std::move(regions)), m_pointCitiesMap(pointCitiesMap)
  {
    SplitRegionsByAdminCenter();
    FillMap();
  }

  RegionsBuilder::Regions && GetFixedRegions()
  {
    RegionLocalityChecker regionsChecker(m_regions);
    SortRegionsByArea();

    size_t countOfFixedRegions = 0;
    auto const kMultCoeff = 10.0;
    auto const kMaxAvaliableArea = kMultCoeff * M_PI * std::pow(Region::GetRadiusByPlaceType(PlaceType::City), 2);
    for (size_t i = 0; i < m_regionsWithAdminCenter.size(); ++i)
    {
      auto & regionWithAdminCenter = m_regionsWithAdminCenter[i];

      if (NeedSkeep(regionWithAdminCenter.GetId()))
        continue;

      // A region is skipped if the region has admin_level=2 or the region already exists
      // in a hierarhy.
      if (regionWithAdminCenter.IsCountry() || !regionWithAdminCenter.GetLabel().empty())
        continue;

      if (regionWithAdminCenter.GetArea() > kMaxAvaliableArea)
        continue;

      auto const id = regionWithAdminCenter.GetAdminCenterId();
      if (m_pointCitiesMap.count(id) == 0)
        continue;

      auto const & adminCenter = m_pointCitiesMap.at(id);
      if (regionsChecker.CityExistsAsRegion(adminCenter))
        continue;

      LOG(LINFO, ("Make the region form the city. Source objects:"
                  "city[", adminCenter.GetId(), ", with name", adminCenter.GetName(),
                  "], region[", regionWithAdminCenter.GetId(), ", with name", regionWithAdminCenter.GetName(), "]"));
      ++countOfFixedRegions;

      CrossOut(adminCenter.GetId());
      SetCityBestAttributesToRegion(adminCenter, regionWithAdminCenter);
    }

    LOG(LINFO, ("City boundaries restored from regions:", countOfFixedRegions));
    std::move(std::begin(m_regionsWithAdminCenter), std::end(m_regionsWithAdminCenter),
              std::back_inserter(m_regions));
    return std::move(m_regions);
  }

private:
  bool NeedSkeep(base::GeoObjectId const & id) const { return m_unsuitable.count(id) != 0; }

  void CrossOut(base::GeoObjectId const & id)
  {
    auto const range = m_adminCenterToRegions.equal_range(id);
    for (auto it = range.first; it != range.second; ++it)
      m_unsuitable.insert(it->second);
  }

  void FillMap()
  {
    for (auto const & region : m_regionsWithAdminCenter)
      m_adminCenterToRegions.emplace(region.GetAdminCenterId(), region.GetId());
  }

  void SplitRegionsByAdminCenter()
  {
    auto const pred = [](Region const & region) {
      return region.HasAdminCenter();
    };
    std::copy_if(std::begin(m_regions), std::end(m_regions),
                 std::back_inserter(m_regionsWithAdminCenter), pred);
    base::EraseIf(m_regions, pred);
  }

  void SortRegionsByArea()
  {
    std::sort(std::begin(m_regions), std::end(m_regions),  [](Region const & l, Region const & r) {
      return l.GetArea() < r.GetArea();
    });
  }

  RegionsBuilder::Regions m_regions;
  PointCitiesMap const & m_pointCitiesMap;
  RegionsBuilder::Regions m_regionsWithAdminCenter;
  std::set<base::GeoObjectId> m_unsuitable;
  std::multimap<base::GeoObjectId, base::GeoObjectId> m_adminCenterToRegions;
};

class RegionsFixerWithPlacePointApproximation
{
public:
  RegionsFixerWithPlacePointApproximation(RegionsBuilder::Regions && regions,
                                          PointCitiesMap const & pointCitiesMap)
    : m_regions(std::move(regions)), m_pointCitiesMap(pointCitiesMap) {}


  RegionsBuilder::Regions && GetFixedRegions()
  {
    RegionLocalityChecker regionsChecker(m_regions);
    RegionsBuilder::Regions additivedRegions;
    size_t countOfFixedRegions = 0;
    for (auto const & cityKeyValue : m_pointCitiesMap)
    {
      auto const & city = cityKeyValue.second;
      if (!regionsChecker.CityExistsAsRegion(city) && NeedCity(city))
      {
        additivedRegions.push_back(Region(city));
        ++countOfFixedRegions;
      }
    }

    LOG(LINFO, ("City boundaries restored by approximation:", countOfFixedRegions));
    std::move(std::begin(additivedRegions), std::end(additivedRegions),
              std::back_inserter(m_regions));
    return std::move(m_regions);
  }

private:
  bool NeedCity(const City & city)
  {
    return city.HasPlaceType() && city.GetPlaceType() != PlaceType::Locality;
  }

  RegionsBuilder::Regions m_regions;
  PointCitiesMap const & m_pointCitiesMap;
};
}  // namespace

void FixRegionsWithAdminCenter(PointCitiesMap const & pointCitiesMap,
                               RegionsBuilder::Regions & regions)
{
  RegionsFixerWithAdminCenter fixer(std::move(regions), pointCitiesMap);
  regions = fixer.GetFixedRegions();
}

void FixRegionsWithPlacePointApproximation(PointCitiesMap const & pointCitiesMap,
                                           RegionsBuilder::Regions & regions)
{
  RegionsFixerWithPlacePointApproximation fixer(std::move(regions), pointCitiesMap);
  regions = fixer.GetFixedRegions();
}
}  // namespace regions
}  // namespace generator

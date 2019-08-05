#include "generator/affiliation.hpp"

#include "platform/platform.hpp"

#include "geometry/mercator.hpp"

#include "base/thread_pool_computational.hpp"

#include <boost/geometry/geometries/register/point.hpp>
#include <boost/geometry/geometries/register/ring.hpp>

BOOST_GEOMETRY_REGISTER_POINT_2D(m2::PointD, double, boost::geometry::cs::cartesian, x, y);
BOOST_GEOMETRY_REGISTER_RING(std::vector<m2::PointD>);

namespace feature
{
CountriesFilesAffiliation::CountriesFilesAffiliation(std::string const & borderPath, bool haveBordersForWholeWorld)
  : m_countries(borders::PackedBorders::GetOrCreate(borderPath))
  , m_haveBordersForWholeWorld(haveBordersForWholeWorld)
{
}

std::vector<std::string> CountriesFilesAffiliation::GetAffiliations(FeatureBuilder const & fb) const
{
  std::vector<std::string> countries;
  std::vector<std::reference_wrapper<borders::CountryPolygons const>> countriesContainer;
  m_countries.ForEachInRect(fb.GetLimitRect(), [&](auto const & countryPolygons) {
    countriesContainer.emplace_back(countryPolygons);
  });

  // todo(m.andrianov): We need to explore this optimization better. There is a hypothesis: some
  // elements belong to a rectangle, but do not belong to the exact boundary.
  if (m_haveBordersForWholeWorld && countriesContainer.size() == 1)
  {
    borders::CountryPolygons const & countryPolygons = countriesContainer.front();
    countries.emplace_back(countryPolygons.GetName());
    return countries;
  }

  for (borders::CountryPolygons const & countryPolygons : countriesContainer)
  {
    auto const need = fb.ForAnyGeometryPoint([&](auto const & point) {
      return countryPolygons.Contains(point);
    });

    if (need)
      countries.emplace_back(countryPolygons.GetName());
  }

  return countries;
}

bool CountriesFilesAffiliation::HasRegionByName(std::string const & name) const
{
  return m_countries.HasRegionByName(name);
}

CountriesFilesIndexAffiliation::CountriesFilesIndexAffiliation(std::string const & borderPath,
                                                               bool haveBordersForWholeWorld)
  : CountriesFilesAffiliation(borderPath, haveBordersForWholeWorld)
{

  static std::mutex cacheMutex;
  static std::unordered_map<std::string, std::shared_ptr<Tree>> cache;
  std::string const key = borderPath + std::to_string(haveBordersForWholeWorld);
  {
    std::lock_guard<std::mutex> lock(cacheMutex);
    auto it = cache.find(key);
    if (it != std::cend(cache))
    {
      m_index = it->second;
      return;
    }
  }

  auto const net = MakeNet(0.1 /* xstep */, 0.1 /* ystep */);
  auto const index = BuildIndex(net);
  {
    std::lock_guard<std::mutex> lock(cacheMutex);
    m_index = index;
    cache.emplace(key, index);
  }
}

std::vector<std::string> CountriesFilesIndexAffiliation::GetAffiliations(FeatureBuilder const & fb) const
{
  namespace bgi = boost::geometry::index;

  std::vector<string> result;
  std::unordered_set<borders::CountryPolygons const *> countires;
  fb.ForEachOuterGeometryPoint([&](auto const & point) {
    std::vector<Value> values;
    bgi::query(*m_index, bgi::covers(point), std::back_inserter(values));
    for (auto const & v : values)
    {
      if (v.second.size() == 1)
      {
        borders::CountryPolygons const & cp = v.second.front();
        if (countires.insert(&cp).second)
          result.emplace_back(cp.GetName());
      }
      else
      {
        for (borders::CountryPolygons const & cp : v.second)
        {
          if (cp.Contains(point) && countires.insert(&cp).second)
            result.emplace_back(cp.GetName());
        }
      }
    }
  });

  return result;
}

// static
std::vector<CountriesFilesIndexAffiliation::Box>
CountriesFilesIndexAffiliation::MakeNet(double xstep, double ystep)
{
  std::vector<Box> net;
  double xmin = MercatorBounds::kMinX;
  double ymin = MercatorBounds::kMinY;
  double x = xmin;
  double y = ymin;
  while (xmin < MercatorBounds::kMaxX)
  {
    x += xstep;
    while (ymin < MercatorBounds::kMaxY)
    {
      y += ystep;
      net.emplace_back(m2::PointD{xmin, ymin}, m2::PointD{x, y});
      ymin = y;
    }

    ymin = MercatorBounds::kMinY;
    y = ymin;
    xmin = x;
  }

  return net;
}

std::shared_ptr<CountriesFilesIndexAffiliation::Tree>
CountriesFilesIndexAffiliation::BuildIndex(std::vector<Box> const & net)
{
  std::vector<Value> treeCells;
  auto const numThreads = GetPlatform().CpuCores();
  base::thread_pool::computational::ThreadPool pool(numThreads);
  std::mutex mutex;
  for (auto const & cell : net)
  {
    pool.SubmitWork([&, cell{cell}]() {
      std::vector<std::reference_wrapper<borders::CountryPolygons const>> countries;
      m_countries.ForEachInRect({cell.min_corner(), cell.max_corner()}, [&](auto const & country) {
        countries.emplace_back(country);
      });

      if (countries.empty())
      {
        return;
      }
      else if (countries.size() == 1)
      {
        std::lock_guard<std::mutex> lock(mutex);
        treeCells.emplace_back(cell, countries);
      }
      else
      {
        std::vector<std::reference_wrapper<borders::CountryPolygons const>> interCountries;
        for (borders::CountryPolygons const & cp : countries)
        {
          cp.ForEachRegionGeometry([&](auto const & geometry) {
            if (!boost::geometry::intersects(geometry, cell))
              return false;

            interCountries.emplace_back(cp);
            return true;
          });
        }

        if (interCountries.empty())
          return;

        std::lock_guard<std::mutex> lock(mutex);
        treeCells.emplace_back(cell, interCountries);
      }
    });
  }

  pool.WaitAndStop();
  return std::make_shared<Tree>(treeCells);
}

SingleAffiliation::SingleAffiliation(std::string const & filename)
  : m_filename(filename)
{
}

std::vector<std::string> SingleAffiliation::GetAffiliations(FeatureBuilder const &) const
{
  return {m_filename};
}

bool SingleAffiliation::HasRegionByName(std::string const & name) const
{
  return name == m_filename;
}
}  // namespace feature

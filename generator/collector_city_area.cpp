#include "generator/collector_city_area.hpp"

#include "generator/feature_generator.hpp"
#include "generator/intermediate_data.hpp"

#include "indexer/ftypes_matcher.hpp"

#include "platform/platform.hpp"

#include "coding/internal/file_data.hpp"

#include "base/assert.hpp"

#include <algorithm>
#include <iterator>

using namespace feature;

namespace generator
{
CityAreaCollector::CityAreaCollector(std::string const & filename)
  : CollectorInterface(filename), m_witer(GetTmpFilename()) {}

std::shared_ptr<CollectorInterface>
CityAreaCollector::Clone(std::shared_ptr<cache::IntermediateDataReader> const &) const
{
  return std::make_shared<CityAreaCollector>(GetFilename());
}

void CityAreaCollector::CollectFeature(FeatureBuilder const & feature, OsmElement const &)
{
  if (!(feature.IsArea() && ftypes::IsCityTownOrVillage(feature.GetTypes())))
    return;

  auto copy = feature;
  if (copy.PreSerialize())
    m_witer.Write(copy);
}

void CityAreaCollector::Flush()
{
  m_witer.Flush();
}

void CityAreaCollector::Save()
{
  CHECK(base::CopyFileX(GetTmpFilename(), GetFilename()), ());
}

void CityAreaCollector::Merge(generator::CollectorInterface const & collector)
{
  collector.MergeInto(*this);
}

void CityAreaCollector::MergeInto(CityAreaCollector & collector) const
{
  using namespace serialization_policy;
  ForEachFromDatRawFormat<MaxAccuracy>(GetTmpFilename(), [&](auto const & fb, auto const &) {
    collector.m_witer.Write(fb);
  });
}
}  // namespace generator

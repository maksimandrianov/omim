#pragma once

#include "generator/regions/city.hpp"
#include "generator/regions/regions_builder.hpp"

#include "base/geo_object_id.hpp"

#include <vector>

namespace generator
{
namespace regions
{
// This function will try to supplement region boundaryies with information from place points
// with a admin_center link.
void FixRegionsWithAdminCenter(PointCitiesMap const & pointCitiesMap,
                               RegionsBuilder::Regions & regions);

// This function will build a boundary from point based on place.
void FixRegionsWithPlacePointApproximation(PointCitiesMap const & pointCitiesMap,
                                           RegionsBuilder::Regions & regions);
}  // namespace regions
}  // namespace generator

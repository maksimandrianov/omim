#include "generator/filter_complex.hpp"

#include "generator/feature_builder.hpp"
#include "generator/hierarchy.hpp"

#include "indexer/ftypes_matcher.hpp"

namespace generator
{
std::shared_ptr<FilterInterface> FilterComplex::Clone() const
{
  return std::make_shared<FilterComplex>();
}

bool FilterComplex::IsAccepted(feature::FeatureBuilder const & fb) const
{
  if (!fb.IsArea() && !fb.IsPoint())
    return false;

  return hierarchy::GetMainType(fb.GetTypes()) != ftype::GetEmptyValue();
}

std::shared_ptr<FilterInterface> FilterComplexPopularity::Clone() const
{
  return std::make_shared<FilterComplexPopularity>();
}

bool FilterComplexPopularity::IsAccepted(feature::FeatureBuilder const & fb) const
{
  auto const & buildingPartChecker = ftypes::IsBuildingPartChecker::Instance();
  return FilterComplex::IsAccepted(fb) && !buildingPartChecker(fb.GetTypes());
}
}  // namespace generator

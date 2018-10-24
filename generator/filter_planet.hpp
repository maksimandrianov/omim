#pragma once

#include "generator/filter_interface.hpp"

namespace generator
{
class FilterPlanet : public FilterInterface
{
public:
  bool Need(OsmElement const & element) override;
  bool Need(FeatureBuilder1 const & feature) override;
};
}  // namespace generator

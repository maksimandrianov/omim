#include "generator/filter_collection.hpp"

#include "generator/feature_builder.hpp"
#include "generator/osm_element.hpp"

namespace generator
{
bool FilterCollection::Need(OsmElement const & element)
{
  if (m_collection.empty())
    return true;

  for (auto & f : m_collection)
  {
    if (!f->Need(element))
      return false;
  }

  return true;
}

bool FilterCollection::Need(FeatureBuilder1 const & feature)
{
  if (m_collection.empty())
    return true;

  for (auto & f : m_collection)
  {
    if (!f->Need(feature))
      return false;
  }

  return true;
}
}  // namespace generator

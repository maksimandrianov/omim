#include "generator/type_helper.hpp"

#include "indexer/classificator.hpp"

namespace generator
{
TypeHelper::TypeHelper()
{
  Classificator const & c = classif();
  char const * arr[][2] = {
    {"natural", "coastline"},
    {"natural", "land"},
    {"place", "island"},
    {"place", "islet"}
  };
  static_assert(ARRAY_SIZE(arr) == static_cast<size_t>(TypeIndex::TYPES_COUNT), "");

  for (size_t i = 0; i < ARRAY_SIZE(arr); ++i)
    m_types[i] = c.GetTypeByPath({arr[i][0], arr[i][1]});
}

// static
uint32_t TypeHelper::GetPlaceType(FeatureParams const & params)
{
  static uint32_t const placeType = classif().GetTypeByPath({"place"});
  return params.FindType(placeType, 1 /* level */);
}
}  // namespace generator

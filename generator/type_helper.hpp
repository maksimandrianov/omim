#pragma once

#include "indexer/feature_data.hpp"
#include "indexer/ftypes_matcher.hpp"

#include <cstdint>

#include <boost/noncopyable.hpp>

namespace generator
{
class TypeHelper : public boost::noncopyable
{
public:
  enum class TypeIndex : size_t
  {
    NATURAL_COASTLINE = 0,
    NATURAL_LAND,
    PLACE_ISLAND,
    PLACE_ISLET,

    TYPES_COUNT
  };

  DECLARE_CHECKER_INSTANCE(TypeHelper);

  static uint32_t GetPlaceType(FeatureParams const & params);
  uint32_t Type(TypeIndex i) const { return m_types[static_cast<size_t>(i)]; }

private:
  TypeHelper();
  uint32_t m_types[static_cast<size_t>(TypeIndex::TYPES_COUNT)];
};
} // namespace generator

#pragma once

#include "generator/feature_builder.hpp"
#include "generator/ways_merger.hpp"

#include <cstdint>

struct OsmElement;

namespace generator
{
namespace cache
{
class IntermediateDataReader;
}  // namespace cache

class HolesAccumulator
{
public:
  explicit HolesAccumulator(cache::IntermediateDataReader & holder);

  void operator() (uint64_t id) { m_merger.AddWay(id); }
  FeatureBuilder1::Geometry & GetHoles();

private:
  AreaWayMerger m_merger;
  FeatureBuilder1::Geometry m_holes;
};

/// Find holes for way with 'id' in first relation.
class HolesProcessor
{
public:
  HolesProcessor(uint64_t id, cache::IntermediateDataReader & holder);

  /// 1. relations process function
  bool operator() (uint64_t /*id*/, RelationElement const & e);
  /// 2. "ways in relation" process function
  void operator() (uint64_t id, std::string const & role);
  FeatureBuilder1::Geometry & GetHoles() { return m_holes.GetHoles(); }

private:
  uint64_t m_id;      ///< id of way to find it's holes
  HolesAccumulator m_holes;
};

class HolesRelation
{
public:
  HolesRelation(cache::IntermediateDataReader & holder);

  void Build(OsmElement const * p);
  FeatureBuilder1::Geometry & GetHoles() { return m_holes.GetHoles(); }
  AreaWayMerger & GetOuter() { return m_outer; }

private:
  HolesAccumulator m_holes;
  AreaWayMerger m_outer;
};
}  // namespace generator

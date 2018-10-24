#pragma once

#include "generator/collection_base.hpp"
#include "generator/collector_interface.hpp"

#include <memory>

struct OsmElement;
class RelationElement;
class FeatureBuilder1;
namespace generator
{
// This class allows you to work with a group of collectors as with one.
class CollectorCollection : public CollectionBase<std::shared_ptr<CollectorInterface>>
{
public:
  void Collect(OsmElement const & element);
  void CollectRelation(RelationElement const & element);
  void CollectFeature(FeatureBuilder1 const & feature, OsmElement const & element);
  void Save();
};
}  // namespace generator

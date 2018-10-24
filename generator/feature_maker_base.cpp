#include "generator/feature_maker_base.hpp"

#include "generator/osm_element.hpp"

namespace generator
{
FeatureMakerBase::FeatureMakerBase(cache::IntermediateDataReader & holder) : m_holder(holder) { }

bool FeatureMakerBase::Add(OsmElement & p)
{
  FeatureParams params;
  ParseParams(params, p);
  switch (p.type)
  {
  case OsmElement::EntityType::Node:
    return BuildFromNode(p, params);
  case OsmElement::EntityType::Way:
    return BuildFromWay(p, params);
  case OsmElement::EntityType::Relation:
    return BuildFromRealtion(p, params);
  default:
    return false;
  }
}

size_t FeatureMakerBase::Size() const
{
  return m_queue.size();
}

bool FeatureMakerBase::Empty() const
{
  return m_queue.empty();
}

bool FeatureMakerBase::GetFeature(FeatureBuilder1 & feature)
{
  if (m_queue.empty())
    return false;

  feature = std::move(m_queue.front());
  m_queue.pop();
  return true;
}


void TransformAreaToPoint(FeatureBuilder1 & feature)
{
  auto const center = feature.GetGeometryCenter();
  auto const id = feature.GetLastOsmId();
  feature.ResetGeometry();
  feature.SetOsmId(id);
  feature.SetCenter(center);
}

void TransformAreaToLine(FeatureBuilder1 & feature)
{
  CHECK(feature.IsGeometryClosed(), ());
  feature.SetLinear(feature.GetParams().m_reverseGeometry);
}

FeatureBuilder1 MakePointFromArea(FeatureBuilder1 const & feature)
{
  FeatureBuilder1 tmp(feature);
  TransformAreaToPoint(tmp);
  return tmp;
}

FeatureBuilder1 MakeLineFromArea(FeatureBuilder1 const & feature)
{
  FeatureBuilder1 tmp(feature);
  TransformAreaToLine(tmp);
  return tmp;
}
}  // namespace generator

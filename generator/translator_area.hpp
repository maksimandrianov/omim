#pragma once

#include "generator/emitter_interface.hpp"
#include "generator/tag_admixer.hpp"
#include "generator/translator.hpp"

#include <memory>

namespace feature
{
struct GenerateInfo;
}  // namespace feature
namespace cache
{
class IntermediateDataReader;
}  // namespace cache

namespace generator
{
// The TranslatorArea class implements translator for building areas.
class TranslatorArea : public Translator
{
public:
  explicit TranslatorArea(std::shared_ptr<EmitterInterface> emitter, cache::IntermediateDataReader & holder,
                          feature::GenerateInfo const & info);

  // TranslatorInterface overrides:
  void Preprocess(OsmElement * p) override;

private:
  void CollectFromRelations(OsmElement const * p);

  TagAdmixer m_tagAdmixer;
  TagReplacer m_tagReplacer;
  OsmTagMixer m_osmTagMixer;
};
}  // namespace generator

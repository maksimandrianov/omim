#pragma once

#include "generator/collection_base.hpp"
#include "generator/translator_interface.hpp"

#include <memory>

namespace generator
{
class TranslatorCollection : public CollectionBase<std::shared_ptr<TranslatorInterface>>
{
public:
  void EmitElement(OsmElement * p);
  bool Finish();
  void GetNames(std::vector<std::string> & names) const;
};
}  // namespace generator

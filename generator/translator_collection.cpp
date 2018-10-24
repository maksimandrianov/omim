#include "generator/translator_collection.hpp"

#include "generator/osm_element.hpp"

#include <algorithm>
#include <iterator>

namespace generator
{
void TranslatorCollection::EmitElement(OsmElement * p)
{
  for (auto & t : m_collection)
  {
    OsmElement copy = *p;
    t->Emit(&copy);
  }
}

bool TranslatorCollection::Finish()
{
  if (m_collection.empty())
    return true;

  for (auto & t : m_collection)
  {
    if (!t->Finish())
      return false;
  }

  return true;
}

void TranslatorCollection::GetNames(std::vector<std::string> & names) const
{
  for (auto & t : m_collection)
  {
    std::vector<std::string> temp;
    t->GetNames(temp);
    std::copy(std::begin(temp), std::end(temp), std::back_inserter(names));
  }
}
}  // namespace generator

#include "generator/translator_coastline.hpp"

#include "generator/feature_maker.hpp"
#include "generator/filter_planet.hpp"
#include "generator/filter_elements.hpp"
#include "generator/generate_info.hpp"
#include "generator/intermediate_data.hpp"
#include "generator/type_helper.hpp"

#include "platform/platform.hpp"

#include "coding/file_name_utils.hpp"

#include "defines.hpp"

namespace generator
{
namespace
{
class CoastlineFilter : public FilterInterface
{
public:
  bool Need(FeatureBuilder1 const & feature)
  {
    auto const & params = feature.GetParams();
    return params.IsTypeExist(TypeHelper::Instance().Type(TypeHelper::TypeIndex::NATURAL_COASTLINE));
  }
};
}  // namespace

TranslatorCoastline::TranslatorCoastline(std::shared_ptr<EmitterInterface> emitter,
                                         cache::IntermediateDataReader & holder)
  : Translator(emitter, holder, std::make_shared<FeatureMaker>(holder))
{
  AddFilter(std::make_shared<FilterPlanet>());
  AddFilter(std::make_shared<CoastlineFilter>());
  AddFilter(std::make_shared<FilterElements>(base::JoinPath(GetPlatform().ResourcesDir(), SKIPPED_ELEMENTS_FILE)));
}
}  // namespace generator

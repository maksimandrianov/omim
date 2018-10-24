#include "generator/emitter_simple.hpp"

#include "generator/feature_builder.hpp"

#include "base/macros.hpp"

namespace generator
{
EmitterSimple::EmitterSimple(feature::GenerateInfo const & info) :
  m_regionGenerator(std::make_unique<SimpleGenerator>(info)) {}

void EmitterSimple::GetNames(std::vector<std::string> & names) const
{
  if (m_regionGenerator)
    names = m_regionGenerator->Parent().Names();
  else
    names.clear();
}

void EmitterSimple::Process(FeatureBuilder1 & fb)
{
  (*m_regionGenerator)(fb);
}

void EmitterPreserialize::Process(FeatureBuilder1 & fb)
{
    UNUSED_VALUE(fb.PreSerialize());
    static std::ofstream f("/home/andrianov/Projects/tr_new.txt");
    f << DebugPrint(fb) << '\n';
    EmitterSimple::Process(fb);
}
}  // namespace generator

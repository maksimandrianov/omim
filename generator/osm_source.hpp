#pragma once

#include "generator/emitter_interface.hpp"
#include "generator/generate_info.hpp"
#include "generator/intermediate_data.hpp"
#include "generator/osm_element.hpp"
#include "generator/translator_collection.hpp"

#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "boost/noncopyable.hpp"

class FeatureBuilder1;
class FeatureParams;

namespace generator
{
class SourceReader
{
  struct Deleter
  {
    bool m_needDelete;
    Deleter(bool needDelete = true) : m_needDelete(needDelete) {}
    void operator()(std::istream * s) const
    {
      if (m_needDelete)
        delete s;
    }
  };

  std::unique_ptr<std::istream, Deleter> m_file;

public:
  SourceReader();
  explicit SourceReader(std::string const & filename);
  explicit SourceReader(std::istringstream & stream);

  uint64_t Read(char * buffer, uint64_t bufferSize);
};

class LoaderWrapper
{
public:
  LoaderWrapper(feature::GenerateInfo & info);
  cache::IntermediateDataReader & GetReader();

private:
  cache::IntermediateDataReader m_reader;
};

class CacheLoader : boost::noncopyable
{
public:
  CacheLoader(feature::GenerateInfo & info);
  cache::IntermediateDataReader & GetCache();

private:
  feature::GenerateInfo & m_info;
  std::unique_ptr<LoaderWrapper> m_loader;
};

bool GenerateRaw(feature::GenerateInfo & info, TranslatorCollection & translators);
bool GenerateRegionFeatures(feature::GenerateInfo & info);
bool GenerateGeoObjectsFeatures(feature::GenerateInfo & info);

bool GenerateIntermediateData(feature::GenerateInfo & info);

void ProcessOsmElementsFromO5M(SourceReader & stream, std::function<void(OsmElement *)> processor);
void ProcessOsmElementsFromXML(SourceReader & stream, std::function<void(OsmElement *)> processor);
}  // namespace generator

#pragma once

#include "generator/feature_generator.hpp"
#include "generator/final_processor_intermediate_mwm.hpp"
#include "generator/processor_interface.hpp"
#include "generator/generate_info.hpp"
#include "generator/intermediate_data.hpp"
#include "generator/osm_o5m_source.hpp"
#include "generator/osm_xml_source.hpp"
#include "generator/translator_collection.hpp"
#include "generator/translator_interface.hpp"

#include "coding/parse_xml.hpp"

#include "base/thread_pool_computational.hpp"
#include "base/thread_safe_queue.hpp"

#include <functional>
#include <iostream>
#include <memory>
#include <queue>
#include <sstream>
#include <string>
#include <vector>

struct OsmElement;
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

bool GenerateIntermediateData(feature::GenerateInfo & info);

void ProcessOsmElementsFromO5M(SourceReader & stream, std::function<void(OsmElement *)> processor);
void ProcessOsmElementsFromXML(SourceReader & stream, std::function<void(OsmElement *)> processor);

class ProcessorOsmElementsInterface
{
public:
  virtual ~ProcessorOsmElementsInterface() = default;

  virtual bool TryRead(OsmElement & element) = 0;
};

class ProcessorOsmElementsFromO5M : public ProcessorOsmElementsInterface
{
public:
  explicit ProcessorOsmElementsFromO5M(SourceReader & stream);

  // ProcessorOsmElementsInterface overrides:
  bool TryRead(OsmElement & element) override;

private:
  SourceReader & m_stream;
  osm::O5MSource m_dataset;
  osm::O5MSource::Iterator m_pos;
};

class ProcessorXmlElementsFromXml : public ProcessorOsmElementsInterface
{
public:
  explicit ProcessorXmlElementsFromXml(SourceReader & stream);

  // ProcessorOsmElementsInterface overrides:
  bool TryRead(OsmElement & element) override;

private:
  bool TryReadFromQueue(OsmElement & element);

  SourceReader & m_stream;
  XMLSource m_xmlSource;
  ParserXMLSequence<SourceReader, XMLSource> m_parser;
  std::queue<OsmElement> m_queue;
};

class TranslatorsPool
{
public:
  explicit TranslatorsPool(std::shared_ptr<TranslatorCollection> const & original,
                           std::shared_ptr<generator::cache::IntermediateData> const & cache,
                           size_t copyCount);

  void Emit(std::vector<OsmElement> && elements);
  bool Finish();

private:
  std::vector<std::shared_ptr<TranslatorInterface>> m_translators;
  base::thread_pool::computational::ThreadPool m_threadPool;
  base::threads::ThreadSafeQueue<base::threads::DataWrapper<size_t>> m_freeTranslators;
};

class RawGeneratorWriter
{
public:
  RawGeneratorWriter(std::shared_ptr<FeatureProcessorQueue> const & queue,
                     std::string const & path);
  ~RawGeneratorWriter();

  void Run();
  std::vector<std::string> GetNames();

private:
  using FeatureBuilderWriter = feature::FeatureBuilderWriter<feature::serialization_policy::MaxAccuracy>;

  void Write(std::vector<ProcessedData> const & vecChanks);
  void ShutdownAndJoin();

  std::thread m_thread;
  std::shared_ptr<FeatureProcessorQueue> m_queue;
  std::string m_path;
  std::unordered_map<std::string, std::unique_ptr<FileWriter>> m_collectors;
};

class RawGenerator
{
public:
  explicit RawGenerator(feature::GenerateInfo & genInfo, size_t threadsCount = 1,
                        size_t chankSize = 1024);

  void GenerateCountries(bool disableAds = true);
  void GenerateWorld(bool disableAds = true);
  void GenerateCoasts();
  void GenerateRegionFeatures(std::string const & filename);
  void GenerateStreetsFeatures(std::string const & filename);
  void GenerateGeoObjectsFeatures(std::string const & filename);
  void GenerateCustom(std::shared_ptr<TranslatorInterface> const & translator);
  void GenerateCustom(std::shared_ptr<TranslatorInterface> const & translator,
                      std::shared_ptr<FinalProcessorIntermediateMwmInterface> const & finalProcessor);

  bool Execute();

  std::vector<std::string> GetNames() const;

  void ForceReloadCache();
  std::shared_ptr<FeatureProcessorQueue> GetQueue();

private:
  using FinalProcessorPtr = std::shared_ptr<FinalProcessorIntermediateMwmInterface>;

  struct FinalProcessorPtrCmp
  {
    bool operator()(FinalProcessorPtr const & l, FinalProcessorPtr const & r)
    {
      return *l < *r;
    }
  };

  FinalProcessorPtr CreateCoslineFinalProcessor();
  FinalProcessorPtr CreateCountryFinalProcessor();
  FinalProcessorPtr CreateWorldFinalProcessor();

  bool GenerateFilteredFeatures();

  feature::GenerateInfo & m_genInfo;
  size_t m_threadsCount;
  size_t m_chankSize;
  std::shared_ptr<generator::cache::IntermediateData> m_cache;
  std::shared_ptr<FeatureProcessorQueue> m_queue;
  std::shared_ptr<TranslatorCollection> m_translators;
  std::priority_queue<FinalProcessorPtr, std::vector<FinalProcessorPtr>, FinalProcessorPtrCmp> m_finalProcessors;
  std::vector<std::string> m_names;
};
}  // namespace generator

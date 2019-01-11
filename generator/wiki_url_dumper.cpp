#include "generator/wiki_url_dumper.hpp"

#include "generator/feature_builder.hpp"

#include "indexer/classificator.hpp"
#include "indexer/feature_processor.hpp"
#include "indexer/ftypes_matcher.hpp"

#include "base/assert.hpp"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <utility>

#include "3party/ThreadPool/ThreadPool.h"

namespace generator
{
WikiUrlDumper::WikiUrlDumper(std::string const & path, std::vector<std::string> const & dataFiles)
  : m_path(path), m_dataFiles(dataFiles) {}

void WikiUrlDumper::Dump(size_t cpuCount) const
{
  CHECK_GREATER(cpuCount, 0, ());

  ThreadPool threadPool(cpuCount);
  std::vector<std::future<std::string>> futures;
  futures.reserve(m_dataFiles.size());

  auto const fn = [](std::string const & filename) {
    std::stringstream stringStream;
    DumpOne(filename, stringStream);
    return stringStream.str();
  };

  for (auto const & path : m_dataFiles)
  {
    auto result = threadPool.enqueue(fn, path);
    futures.emplace_back(std::move(result));
  }

  std::ofstream stream;
  stream.exceptions(std::fstream::failbit | std::fstream::badbit);
  stream.open(m_path);
  stream << "MwmPath\tosmId\twikipediaUrl\n";
  for (auto & f : futures)
  {
    auto lines = f.get();
    stream << lines;
  }
}

// static
void WikiUrlDumper::DumpOne(std::string const & path, std::ostream & stream)
{
  auto const & needWikiUrl = ftypes::WikiChecker::Instance();
  feature::ForEachFromDatRawFormat(path, [&](FeatureBuilder1 const & feature, uint64_t /* pos */) {
    if (!needWikiUrl(feature.GetTypesHolder()))
      return;

    auto const wikiUrl = feature.GetMetadata().GetWikiURL();
    if (wikiUrl.empty())
      return;

    stream << path << "\t" << feature.GetMostGenericOsmId() << "\t" <<  wikiUrl << "\n";
  });
}
}  // namespace generator

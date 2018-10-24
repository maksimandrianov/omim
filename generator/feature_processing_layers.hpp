#pragma once

#include "generator/booking_dataset.hpp"
#include "generator/feature_generator.hpp"
#include "generator/opentable_dataset.hpp"
#include "generator/polygonizer.hpp"
#include "generator/world_map_generator.hpp"

#include <memory>
#include <string>
#include <sstream>

class FeatureBuilder1;
class CoastlineFeaturesGenerator;
namespace feature
{
struct GenerateInfo;
}  // namespace feature
namespace generator
{
class CityBoundaryProcessor;
class CountryMapper;
class WorldMapper;

// Responsibility of the class Log Buffer - encapsulation of the buffer for internal logs.
class LogBuffer
{
public:
  template <class T, class... Ts>
  constexpr void AppendLine(T const & value, Ts... rest)
  {
    Append_(value, rest...);
    m_buffer << "\n";
  }

  std::string GetAsString() const;

private:
  template <class T, class... Ts>
  constexpr void Append_(T const & value, Ts... rest)
  {
    m_buffer << value << "\t";
    Append_(rest...);
  }

  constexpr void Append_() {}

  std::ostringstream m_buffer;
};

// This is the base layer class. Inheriting from it allows you to create a chain of layers.
class LayerBase
{
public:
  LayerBase() = default;
  virtual ~LayerBase() = default;

  virtual void Handle(FeatureBuilder1 & feature);

  void SetNext(std::shared_ptr<LayerBase> next);
  std::shared_ptr<LayerBase> Add(std::shared_ptr<LayerBase> next);

  template <class T, class... Ts>
  constexpr void AppendLine(T const & value, Ts... rest)
  {
    m_logBuffer.AppendLine(value, rest...);
  }

  std::string GetAsString() const;
  std::string GetAsStringRecursive() const;

private:
  LogBuffer m_logBuffer;
  std::shared_ptr<LayerBase> m_next;
};

// Responsibility of class RepresentationLayer is converting features from one form to another for areas.
// Here we can use the knowledge of the rules for drawing objects.
// The class transfers control to the CityBoundaryProcessor if for the feature it is a city, town or village.
class RepresentationLayer : public LayerBase
{
public:
  RepresentationLayer(std::shared_ptr<CityBoundaryProcessor> processor);

  // LayerBase overrides:
  void Handle(FeatureBuilder1 & feature) override;

  static bool PerceiveAsArea(FeatureParams const & params);
  static bool PerceiveAsPoint(FeatureParams const & params);
  static bool PerceiveAsLine(FeatureParams const & params);

  void HandleArea(FeatureBuilder1 & feature, FeatureParams const & params);

  std::shared_ptr<CityBoundaryProcessor> m_processor;
};

// Responsibility of class PrepareFeatureLayer is the removal of unused types and names,
// as well as the preparation of features for further processing for areas.
class PrepareFeatureLayer : public LayerBase
{
public:
  // LayerBase overrides:
  void Handle(FeatureBuilder1 & feature) override;
  static void FixTypeLand(FeatureBuilder1 & feature);
};

// Responsibility of class CityBoundaryLayer - transfering control to the CityBoundaryProcessor
// if the feature is a place.
class CityBoundaryLayer : public LayerBase
{
public:
  CityBoundaryLayer(std::shared_ptr<CityBoundaryProcessor> processor);

  // LayerBase overrides:
  void Handle(FeatureBuilder1 & feature) override;

private:
  std::shared_ptr<CityBoundaryProcessor> m_processor;
};

// Responsibility of class BookingLayer - mixing information from booking. If there is a
// coincidence of the hotel and feature, the processing of the feature is completed.
class BookingLayer : public LayerBase
{
public:
  BookingLayer(std::string const & filename, std::shared_ptr<CountryMapper> countryMapper);

  // LayerBase overrides:
  ~BookingLayer() override;

  void Handle(FeatureBuilder1 & feature) override;

private:
  BookingDataset m_dataset;
  std::shared_ptr<CountryMapper> m_countryMapper;
};

// Responsibility of class OpentableLayer - mixing information from opentable. If there is a
// coincidence of the restaurant and feature, the processing of the feature is completed.
class OpentableLayer : public LayerBase
{
public:
  OpentableLayer(std::string const & filename, std::shared_ptr<CountryMapper> countryMapper);

  // LayerBase overrides:
  void Handle(FeatureBuilder1 & feature) override;

private:
  OpentableDataset m_dataset;
  std::shared_ptr<CountryMapper> m_countryMapper;
};

// Responsibility of class CountryMapperLayer - mapping of features on areas.
class CountryMapperLayer : public LayerBase
{
public:
  CountryMapperLayer(std::shared_ptr<CountryMapper> countryMapper);

  // LayerBase overrides:
  void Handle(FeatureBuilder1 & feature) override;

private:
  std::shared_ptr<CountryMapper> m_countryMapper;
};

// Responsibility of class EmitCoastsLayer is adding coastlines to areas.
class EmitCoastsLayer : public LayerBase
{
public:
  EmitCoastsLayer(std::string const & worldCoastsFilename, std::string const & geometryFilename,
                  std::shared_ptr<CountryMapper> countryMapper);

  // LayerBase overrides:
  ~EmitCoastsLayer() override;

  void Handle(FeatureBuilder1 & feature) override;

private:
  std::shared_ptr<feature::FeaturesCollector> m_collector;
  std::shared_ptr<CountryMapper> m_countryMapper;
  std::string m_geometryFilename;
};

// Responsibility of class RepresentationCoastlineLayer is converting features from one form to another for coastlines.
// Here we can use the knowledge of the rules for drawing objects.
class RepresentationCoastlineLayer : public LayerBase
{
public:
  // LayerBase overrides:
  void Handle(FeatureBuilder1 & feature) override;
};

// Responsibility of class PrepareCoastlineFeatureLayer is the removal of unused types and names,
// as well as the preparation of features for further processing for coastlines.
class PrepareCoastlineFeatureLayer : public LayerBase
{
public:
  // LayerBase overrides:
  void Handle(FeatureBuilder1 & feature) override;
};

// Responsibility of class CoastlineMapperLayer - mapping of features on coastline.
class CoastlineMapperLayer : public LayerBase
{
public:
  CoastlineMapperLayer(std::shared_ptr<CoastlineFeaturesGenerator> coastlineMapper);

  // LayerBase overrides:
  void Handle(FeatureBuilder1 & feature) override;

private:
  std::shared_ptr<CoastlineFeaturesGenerator> m_coastlineGenerator;
};

// Responsibility of class CountryMapperLayer - mapping of features on the world.
class WorldAreaLayer : public LayerBase
{
public:
  using WorldGenerator = WorldMapGenerator<feature::FeaturesCollector>;

  WorldAreaLayer(std::shared_ptr<WorldMapper> mapper);

  // LayerBase overrides:
  ~WorldAreaLayer() override;

  void Handle(FeatureBuilder1 & feature) override;

private:
  std::shared_ptr<WorldMapper> m_mapper;
};

// This is the class-wrapper over CountriesGenerator class.
class CountryMapper
{
public:
  using Polygonizer = feature::Polygonizer<feature::FeaturesCollector>;
  using CountriesGenerator = CountryMapGenerator<Polygonizer>;

  CountryMapper(feature::GenerateInfo const & info);

  void Map(FeatureBuilder1 & feature);
  void RemoveInvalidTypesAndMap(FeatureBuilder1 & feature);
  Polygonizer & Parent();
  std::vector<std::string> const & GetNames() const;

private:
  std::unique_ptr<CountriesGenerator> m_countries;
};

// This is the class-wrapper over WorldGenerator class.
class WorldMapper
{
public:
  using WorldGenerator = WorldMapGenerator<feature::FeaturesCollector>;

  WorldMapper(std::string const & worldFilename, std::string const & rawGeometryFilename,
              std::string const & popularPlacesFilename);

  void Map(FeatureBuilder1 & feature);
  void RemoveInvalidTypesAndMap(FeatureBuilder1 & feature);
  void Merge();

private:
  std::unique_ptr<WorldGenerator> m_world;
};
}  // namespace generator

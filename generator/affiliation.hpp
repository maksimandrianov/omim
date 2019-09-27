#pragma once

#include "generator/borders.hpp"
#include "generator/cells_merger.hpp"
#include "generator/feature_builder.hpp"

#include <string>
#include <vector>

#include <boost/geometry/index/rtree.hpp>
#include <boost/optional.hpp>

namespace feature
{
class AffiliationInterface
{
public:
  virtual ~AffiliationInterface() = default;

  // The method will return the names of the buckets to which the fb belongs.
  virtual std::vector<std::string> GetAffiliations(FeatureBuilder const & fb) const = 0;
  virtual std::vector<std::string> GetAffiliations(m2::PointD const & point) const = 0;
  virtual bool HasRegionByName(std::string const & name) const = 0;
};

class CountriesFilesAffiliation : public AffiliationInterface
{
public:
  CountriesFilesAffiliation(std::string const & borderPath, bool haveBordersForWholeWorld);

  // AffiliationInterface overrides:
  std::vector<std::string> GetAffiliations(FeatureBuilder const & fb) const override;
  std::vector<std::string> GetAffiliations(m2::PointD const & point) const override;

  bool HasRegionByName(std::string const & name) const override;

protected:
  borders::CountriesContainer const & m_countries;
  bool m_haveBordersForWholeWorld;
};

class CountriesFilesIndexAffiliation : public CountriesFilesAffiliation
{
public:
  using Box = boost::geometry::model::box<m2::PointD>;
  using Value = std::pair<Box, std::vector<std::reference_wrapper<borders::CountryPolygons const>>>;
  using Tree = boost::geometry::index::rtree<Value, boost::geometry::index::quadratic<16>>;

  CountriesFilesIndexAffiliation(std::string const & borderPath, bool haveBordersForWholeWorld);

  // AffiliationInterface overrides:
  std::vector<std::string> GetAffiliations(FeatureBuilder const & fb) const override;

private:
  class IndexBuilder
  {
  public:
    IndexBuilder(borders::CountriesContainer const & allCountries, double cellSize,
                 bool haveBordersForWholeWorld);

    std::shared_ptr<Tree> Build();

  private:
    using CountriesRects =
        std::unordered_map<borders::CountryPolygons const *, std::vector<m2::RectD>>;

    CountriesRects AddDisputedCells();
    void AddMergedCells(CountriesRects const & countriesRects);

    std::shared_ptr<Tree> m_tree;
    double m_step = 0.0;
    double const m_cellSize = 0.0;
    std::vector<Value> m_treeCells;
    bool m_haveBordersForWholeWorld = false;
    borders::CountryPolygons m_outdoor;
  };

  static Box MakeBox(m2::RectD const & rect);
  std::shared_ptr<Tree> BuildIndex(double cellSize);
  boost::optional<std::string> IsOneCountryForBbox(FeatureBuilder const & fb) const;
  std::vector<std::string> GetHonestAffiliations(FeatureBuilder const & fb) const;

  std::shared_ptr<Tree> m_index;
};

class SingleAffiliation : public AffiliationInterface
{
public:
  explicit SingleAffiliation(std::string const & filename);

  // AffiliationInterface overrides:
  std::vector<std::string> GetAffiliations(FeatureBuilder const &) const override;
  bool HasRegionByName(std::string const & name) const override;
  std::vector<std::string> GetAffiliations(m2::PointD const & point) const override;

private:
  std::string m_filename;
};
}  // namespace feature

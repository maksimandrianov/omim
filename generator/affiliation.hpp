#pragma once

#include "generator/borders.hpp"
#include "generator/feature_builder.hpp"

#include <string>
#include <vector>

#include <boost/geometry/geometries/box.hpp>
#include <boost/geometry/geometries/point.hpp>
#include <boost/geometry.hpp>
#include <boost/geometry/index/rtree.hpp>

namespace feature
{
class AffiliationInterface
{
public:
  virtual ~AffiliationInterface() = default;

  // The method will return the names of the buckets to which the fb belongs.
  virtual std::vector<std::string> GetAffiliations(FeatureBuilder const & fb) const = 0;
  virtual bool HasRegionByName(std::string const & name) const = 0;
};

class CountriesFilesAffiliation : public AffiliationInterface
{
public:
  CountriesFilesAffiliation(std::string const & borderPath, bool haveBordersForWholeWorld);

  // AffiliationInterface overrides:
  std::vector<std::string> GetAffiliations(FeatureBuilder const & fb) const override;
  bool HasRegionByName(std::string const & name) const override;

protected:
  borders::CountriesContainer const & m_countries;
  bool m_haveBordersForWholeWorld;
};

class CountriesFilesIndexAffiliation : public CountriesFilesAffiliation
{
public:
  CountriesFilesIndexAffiliation(std::string const & borderPath, bool haveBordersForWholeWorld);

  // AffiliationInterface overrides:
  std::vector<std::string> GetAffiliations(FeatureBuilder const & fb) const override;

private:
  using Box = boost::geometry::model::box<m2::PointD>;
  using Value = std::pair<Box, std::vector<std::reference_wrapper<borders::CountryPolygons const>>>;
  using Tree = boost::geometry::index::rtree<Value, boost::geometry::index::quadratic<16>>;

  static std::vector<Box> MakeNet(double xstep, double ystep);
  std::shared_ptr<Tree> BuildIndex(std::vector<Box> const & net);

  std::shared_ptr<Tree> m_index;
};

class SingleAffiliation : public AffiliationInterface
{
public:
  SingleAffiliation(std::string const & filename);

  // AffiliationInterface overrides:
  std::vector<std::string> GetAffiliations(FeatureBuilder const &) const override;
  bool HasRegionByName(std::string const & name) const override;

private:
  std::string m_filename;
};
}  // namespace feature

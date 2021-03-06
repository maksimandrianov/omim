#include "testing/testing.hpp"

#include "generator/generator_tests/regions_common.hpp"
#include "generator/osm_element.hpp"
#include "generator/regions/collector_region_info.hpp"
#include "generator/regions/regions.hpp"
#include "generator/regions/regions_builder.hpp"
#include "generator/regions/to_string_policy.hpp"

#include "platform/platform.hpp"

#include "coding/file_name_utils.hpp"

#include "base/macros.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include <utility>

using namespace generator::regions;
using namespace base;

namespace
{
using Tags = std::vector<std::pair<std::string, std::string>>;

OsmElement MakeOsmElement(uint64_t id, std::string const & adminLevel,
                          std::string const & place = "")
{
  OsmElement el;
  el.id = id;
  el.AddTag("place", place);
  el.AddTag("admin_level", adminLevel);

  return el;
}

void MakeCollectorData()
{
  CollectorRegionInfo collector(GetFileName());
  collector.Collect(MakeOsmRelation(1 /* id */), MakeOsmElement(1 /* id */, "2" /* adminLevel */));
  collector.Collect(MakeOsmRelation(2 /* id */), MakeOsmElement(2 /* id */, "2" /* adminLevel */));
  collector.Collect(MakeOsmRelation(3 /* id */), MakeOsmElement(3 /* id */, "4" /* adminLevel */));
  collector.Collect(MakeOsmRelation(4 /* id */), MakeOsmElement(4 /* id */, "4" /* adminLevel */));
  collector.Collect(MakeOsmRelation(5 /* id */), MakeOsmElement(5 /* id */, "4" /* adminLevel */));
  collector.Collect(MakeOsmRelation(6 /* id */), MakeOsmElement(6 /* id */, "6" /* adminLevel */));
  collector.Collect(MakeOsmRelation(7 /* id */), MakeOsmElement(7 /* id */, "6" /* adminLevel */));
  collector.Collect(MakeOsmRelation(8 /* id */), MakeOsmElement(8 /* id */, "4" /* adminLevel */));
  collector.Save();
}

RegionsBuilder::Regions MakeTestDataSet1(RegionInfo & collector)
{
  RegionsBuilder::Regions regions;
  {
    FeatureBuilder1 fb1;
    fb1.AddName("default", "Country_1");
    fb1.SetOsmId(MakeOsmRelation(1 /* id */));
    vector<m2::PointD> poly = {{2, 8}, {3, 12}, {8, 15}, {13, 12}, {15, 7}, {11, 2}, {4, 4}, {2, 8}};
    fb1.AddPolygon(poly);
    fb1.SetAreaAddHoles({{{5, 8}, {7, 10}, {10, 10}, {11, 7}, {10, 4}, {7, 5}, {5, 8}}});
    regions.emplace_back(Region(fb1, collector.Get(MakeOsmRelation(1 /* id */))));
  }

  {
    FeatureBuilder1 fb1;
    fb1.AddName("default", "Country_2");
    fb1.SetOsmId(MakeOsmRelation(2 /* id */));
    vector<m2::PointD> poly = {{5, 8}, {7, 10}, {10, 10}, {11, 7}, {10, 4}, {7, 5}, {5, 8}};
    fb1.AddPolygon(poly);
    regions.emplace_back(Region(fb1, collector.Get(MakeOsmRelation(2 /* id */))));
  }

  {
    FeatureBuilder1 fb1;
    fb1.AddName("default", "Country_2");
    fb1.SetOsmId(MakeOsmRelation(2 /* id */));
    vector<m2::PointD> poly = {{0, 0}, {0, 2}, {2, 2}, {2, 0}, {0, 0}};
    fb1.AddPolygon(poly);
    regions.emplace_back(Region(fb1, collector.Get(MakeOsmRelation(2 /* id */))));
  }

  {
    FeatureBuilder1 fb1;
    fb1.AddName("default", "Country_1_Region_3");
    fb1.SetOsmId(MakeOsmRelation(3 /* id */));
    vector<m2::PointD> poly = {{4, 4}, {7, 5}, {10, 4}, {12, 9}, {15, 7}, {11, 2}, {4, 4}};
    fb1.AddPolygon(poly);
    regions.emplace_back(Region(fb1, collector.Get(MakeOsmRelation(3 /* id */))));
  }

  {
    FeatureBuilder1 fb1;
    fb1.AddName("default", "Country_1_Region_4");
    fb1.SetOsmId(MakeOsmRelation(4 /* id */));
    vector<m2::PointD> poly = {{7, 10}, {9, 12}, {8, 15}, {13, 12}, {15, 7}, {12, 9},
                               {11, 7}, {10, 10}, {7, 10}};
    fb1.AddPolygon(poly);
    regions.emplace_back(Region(fb1, collector.Get(MakeOsmRelation(4 /* id */))));
  }

  {
    FeatureBuilder1 fb1;
    fb1.AddName("default", "Country_1_Region_5");
    fb1.SetOsmId(MakeOsmRelation(5 /* id */));
    vector<m2::PointD> poly = {{4, 4}, {2, 8}, {3, 12}, {8, 15}, {9, 12}, {7, 10}, {5, 8},
                               {7, 5}, {4, 4}};
    fb1.AddPolygon(poly);
    regions.emplace_back(Region(fb1, collector.Get(MakeOsmRelation(5 /* id */))));
  }

  {
    FeatureBuilder1 fb1;
    fb1.AddName("default", "Country_1_Region_5_Subregion_6");
    fb1.SetOsmId(MakeOsmRelation(6 /* id */));
    vector<m2::PointD> poly = {{4, 4}, {2, 8}, {3, 12}, {4, 10}, {5, 10}, {5, 8}, {7, 5}, {4, 4}};
    fb1.AddPolygon(poly);
    regions.emplace_back(Region(fb1, collector.Get(MakeOsmRelation(6 /* id */))));
  }

  {
    FeatureBuilder1 fb1;
    fb1.AddName("default", "Country_1_Region_5_Subregion_7");
    fb1.SetOsmId(MakeOsmRelation(7 /* id */));
    vector<m2::PointD> poly = {{3, 12}, {8, 15}, {9, 12}, {7, 10}, {5, 8}, {5, 10}, {4, 10}, {3, 12}};
    fb1.AddPolygon(poly);
    regions.emplace_back(Region(fb1, collector.Get(MakeOsmRelation(7 /* id */))));
  }

  {
    FeatureBuilder1 fb1;
    fb1.AddName("default", "Country_2_Region_8");
    fb1.SetOsmId(MakeOsmRelation(8 /* id */));
    vector<m2::PointD> poly = {{0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}};
    fb1.AddPolygon(poly);
    regions.emplace_back(Region(fb1, collector.Get(MakeOsmRelation(8 /* id */))));
  }

  return regions;
}

class Helper : public ToStringPolicyInterface
{
public:
  Helper(std::vector<std::string> & bankOfNames) : m_bankOfNames(bankOfNames) {}

  std::string ToString(Node::PtrList const & nodePtrList) override
  {
    std::stringstream stream;
    for (auto const & n : nodePtrList)
      stream << n->GetData().GetName();

    auto str = stream.str();
    m_bankOfNames.push_back(str);
    return str;
  }

  std::vector<std::string> & m_bankOfNames;
};

bool ExistsName(std::vector<std::string> const & coll, std::string const name)
{
  auto const end = std::end(coll);
  return std::find(std::begin(coll), end, name) != end;
}
;
}  // namespace

UNIT_TEST(RegionsBuilderTest_GetCountryNames)
{
  MakeCollectorData();
  RegionInfo collector(GetFileName());
  RegionsBuilder builder(MakeTestDataSet1(collector));
  auto const countryNames = builder.GetCountryNames();
  TEST_EQUAL(countryNames.size(), 2, ());
  TEST(std::count(std::begin(countryNames), std::end(countryNames), "Country_1"), ());
  TEST(std::count(std::begin(countryNames), std::end(countryNames), "Country_2"), ());
}

UNIT_TEST(RegionsBuilderTest_GetCountries)
{
  MakeCollectorData();
  RegionInfo collector(GetFileName());
  RegionsBuilder builder(MakeTestDataSet1(collector));
  auto const countries = builder.GetCountries();
  TEST_EQUAL(countries.size(), 3, ());
  TEST_EQUAL(std::count_if(std::begin(countries), std::end(countries),
                           [](const Region & r) {return r.GetName() == "Country_1"; }), 1, ());
  TEST_EQUAL(std::count_if(std::begin(countries), std::end(countries),
                           [](const Region & r) {return r.GetName() == "Country_2"; }), 2, ());
}

UNIT_TEST(RegionsBuilderTest_GetCountryTrees)
{
  MakeCollectorData();
  RegionInfo collector(GetFileName());
  std::vector<std::string> bankOfNames;
  RegionsBuilder builder(MakeTestDataSet1(collector), std::make_unique<Helper>(bankOfNames));

  auto const countryTrees = builder.GetCountryTrees();
  for (auto const & countryName : builder.GetCountryNames())
  {
    auto const keyRange = countryTrees.equal_range(countryName);
    for (auto it = keyRange.first; it != keyRange.second; ++it)
    {
      auto const unused = builder.ToIdStringList(it->second);
      UNUSED_VALUE(unused);
    }
  }

  TEST_EQUAL(std::count(std::begin(bankOfNames), std::end(bankOfNames), "Country_2"), 2, ());
  TEST(ExistsName(bankOfNames, "Country_1"), ());
  TEST(ExistsName(bankOfNames, "Country_1_Region_3Country_1"), ());
  TEST(ExistsName(bankOfNames, "Country_1_Region_4Country_1"), ());
  TEST(ExistsName(bankOfNames, "Country_1_Region_5Country_1"), ());
  TEST(ExistsName(bankOfNames, "Country_2_Region_8Country_2"), ());
  TEST(ExistsName(bankOfNames, "Country_1_Region_5_Subregion_6Country_1_Region_5Country_1"), ());
  TEST(ExistsName(bankOfNames, "Country_1_Region_5_Subregion_7Country_1_Region_5Country_1"), ());
}

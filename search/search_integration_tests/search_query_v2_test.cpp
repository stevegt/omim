#include "testing/testing.hpp"

#include "search/search_tests_support/test_feature.hpp"
#include "search/search_tests_support/test_mwm_builder.hpp"
#include "search/search_tests_support/test_results_matching.hpp"
#include "search/search_tests_support/test_search_engine.hpp"
#include "search/search_tests_support/test_search_request.hpp"

#include "search/search_query_factory.hpp"
#include "search/v2/search_query_v2.hpp"

#include "indexer/classificator_loader.hpp"
#include "indexer/index.hpp"
#include "indexer/mwm_set.hpp"

#include "storage/country_decl.hpp"
#include "storage/country_info_getter.hpp"

#include "geometry/point2d.hpp"

#include "platform/country_file.hpp"
#include "platform/local_country_file.hpp"
#include "platform/local_country_file_utils.hpp"
#include "platform/platform.hpp"

#include "base/scope_guard.hpp"

using namespace search::tests_support;

namespace
{
void Cleanup(platform::LocalCountryFile const & map)
{
  platform::CountryIndexes::DeleteFromDisk(map);
  map.DeleteFromDisk(MapOptions::Map);
}

class TestSearchQueryFactory : public search::SearchQueryFactory
{
  // search::SearchQueryFactory overrides:
  unique_ptr<search::Query> BuildSearchQuery(Index & index, CategoriesHolder const & categories,
                                             vector<search::Suggest> const & suggests,
                                             storage::CountryInfoGetter const & infoGetter) override
  {
    return make_unique<search::v2::SearchQueryV2>(index, categories, suggests, infoGetter);
  }
};
}  // namespace

UNIT_TEST(SearchQueryV2_Smoke)
{
  classificator::Load();
  Platform & platform = GetPlatform();
  platform::LocalCountryFile wonderland(platform.WritableDir(), platform::CountryFile("wonderland"),
                                        0);
  platform::LocalCountryFile testWorld(platform.WritableDir(), platform::CountryFile("testWorld"),
                                       0);
  m2::RectD wonderlandRect(m2::PointD(-1.0, -1.0), m2::PointD(10.1, 10.1));
  m2::RectD viewport(m2::PointD(-1.0, -1.0), m2::PointD(1.0, 1.0));

  auto cleanup = [&]()
  {
    Cleanup(wonderland);
    Cleanup(testWorld);
  };

  cleanup();
  MY_SCOPE_GUARD(cleanupAtExit, cleanup);

  vector<storage::CountryDef> countries;
  countries.emplace_back(wonderland.GetCountryName(), wonderlandRect);

  TestSearchEngine engine("en", make_unique<storage::CountryInfoGetterForTesting>(countries),
                          make_unique<TestSearchQueryFactory>());
  auto const mskCity = make_shared<TestCity>(m2::PointD(0, 0), "Moscow", "en", 100 /* rank */);
  auto const busStop = make_shared<TestPOI>(m2::PointD(0, 0), "Bus stop", "en");
  auto const tramStop = make_shared<TestPOI>(m2::PointD(0.0001, 0.0001), "Tram stop", "en");
  auto const quantumTeleport1 =
      make_shared<TestPOI>(m2::PointD(0.0002, 0.0002), "Quantum teleport 1", "en");
  auto const quantumTeleport2 =
      make_shared<TestPOI>(m2::PointD(10, 10), "Quantum teleport 2", "en");
  auto const quantumCafe = make_shared<TestPOI>(m2::PointD(-0.0002, -0.0002), "Quantum cafe", "en");
  auto const feynmanStreet = make_shared<TestStreet>(
      vector<m2::PointD>{m2::PointD(9.999, 9.999), m2::PointD(10, 10), m2::PointD(10.001, 10.001)},
      "Feynman street", "en");
  auto const bohrStreet1 = make_shared<TestStreet>(
      vector<m2::PointD>{m2::PointD(9.999, 10.001), m2::PointD(10, 10), m2::PointD(10.001, 9.999)},
      "Bohr street", "en");
  auto const bohrStreet2 = make_shared<TestStreet>(
      vector<m2::PointD>{m2::PointD(10.001, 9.999), m2::PointD(10.002, 9.998)}, "Bohr street",
      "en");
  auto const bohrStreet3 = make_shared<TestStreet>(
      vector<m2::PointD>{m2::PointD(10.002, 9.998), m2::PointD(10.003, 9.997)}, "Bohr street",
      "en");
  auto const feynmanHouse = make_shared<TestBuilding>(m2::PointD(10, 10), "Feynman house 1 unit 1",
                                                      "1 unit 1", *feynmanStreet, "en");
  auto const bohrHouse = make_shared<TestBuilding>(m2::PointD(10, 10), "Bohr house 1 unit 1 ",
                                                   "1 unit 1", *bohrStreet1, "en");

  auto const hilbertHouse = make_shared<TestBuilding>(
      vector<m2::PointD>{
          {10.0005, 10.0005}, {10.0006, 10.0005}, {10.0006, 10.0006}, {10.0005, 10.0006}},
      "Hilbert house 1 unit 2", "1 unit 2", *bohrStreet1, "en");
  auto const lantern1 = make_shared<TestPOI>(m2::PointD(10.0005, 10.0005), "lantern 1", "en");
  auto const lantern2 = make_shared<TestPOI>(m2::PointD(10.0006, 10.0005), "lantern 2", "en");

  {
    TestMwmBuilder builder(wonderland, feature::DataHeader::country);
    builder.Add(*mskCity);
    builder.Add(*busStop);
    builder.Add(*tramStop);
    builder.Add(*quantumTeleport1);
    builder.Add(*quantumTeleport2);
    builder.Add(*quantumCafe);
    builder.Add(*feynmanStreet);
    builder.Add(*bohrStreet1);
    builder.Add(*bohrStreet2);
    builder.Add(*bohrStreet3);
    builder.Add(*feynmanHouse);
    builder.Add(*bohrHouse);

    builder.Add(*hilbertHouse);
    builder.Add(*lantern1);
    builder.Add(*lantern2);
  }

  {
    TestMwmBuilder builder(testWorld, feature::DataHeader::world);
    builder.Add(*mskCity);
  }

  auto const wonderlandResult = engine.RegisterMap(wonderland);
  TEST_EQUAL(wonderlandResult.second, MwmSet::RegResult::Success, ());

  auto const testWorldResult = engine.RegisterMap(testWorld);
  TEST_EQUAL(testWorldResult.second, MwmSet::RegResult::Success, ());

  auto wonderlandId = wonderlandResult.first;

  {
    TestSearchRequest request(engine, "Bus stop", "en", search::SearchParams::ALL, viewport);
    request.Wait();
    vector<shared_ptr<MatchingRule>> rules = {make_shared<ExactMatch>(wonderlandId, busStop)};
    TEST(MatchResults(engine, rules, request.Results()), ());
  }

  {
    TestSearchRequest request(engine, "quantum", "en", search::SearchParams::ALL, viewport);
    request.Wait();
    vector<shared_ptr<MatchingRule>> rules = {
        make_shared<ExactMatch>(wonderlandId, quantumCafe),
        make_shared<ExactMatch>(wonderlandId, quantumTeleport1),
        make_shared<ExactMatch>(wonderlandId, quantumTeleport2)};
    TEST(MatchResults(engine, rules, request.Results()), ());
  }

  {
    TestSearchRequest request(engine, "quantum Moscow ", "en", search::SearchParams::ALL, viewport);
    request.Wait();
    vector<shared_ptr<MatchingRule>> rules = {
        make_shared<ExactMatch>(wonderlandId, quantumCafe),
        make_shared<ExactMatch>(wonderlandId, quantumTeleport1)};
    TEST(MatchResults(engine, rules, request.Results()), ());
  }

  {
    TestSearchRequest request(engine, "     ", "en", search::SearchParams::ALL, viewport);
    request.Wait();
    TEST_EQUAL(0, request.Results().size(), ());
  }

  {
    TestSearchRequest request(engine, "teleport feynman street", "en", search::SearchParams::ALL,
                              viewport);
    request.Wait();
    vector<shared_ptr<MatchingRule>> rules = {
        make_shared<ExactMatch>(wonderlandId, quantumTeleport2)};
    TEST(MatchResults(engine, rules, request.Results()), ());
  }

  {
    TestSearchRequest request(engine, "feynman street 1", "en", search::SearchParams::ALL,
                              viewport);
    request.Wait();
    vector<shared_ptr<MatchingRule>> rules = {make_shared<ExactMatch>(wonderlandId, feynmanHouse),
                                              make_shared<ExactMatch>(wonderlandId, lantern1)};
    TEST(MatchResults(engine, rules, request.Results()), ());
  }

  {
    TestSearchRequest request(engine, "bohr street 1", "en", search::SearchParams::ALL, viewport);
    request.Wait();
    vector<shared_ptr<MatchingRule>> rules = {make_shared<ExactMatch>(wonderlandId, bohrHouse),
                                              make_shared<ExactMatch>(wonderlandId, hilbertHouse),
                                              make_shared<ExactMatch>(wonderlandId, lantern1)};
    TEST(MatchResults(engine, rules, request.Results()), ());
  }

  {
    TestSearchRequest request(engine, "bohr street 1 unit 3", "en", search::SearchParams::ALL,
                              viewport);
    request.Wait();
    TEST_EQUAL(0, request.Results().size(), ());
  }

  {
    TestSearchRequest request(engine, "bohr street 1 lantern ", "en",
                              search::SearchParams::ALL, viewport);
    request.Wait();
    vector<shared_ptr<MatchingRule>> rules = {make_shared<ExactMatch>(wonderlandId, lantern1),
                                              make_shared<ExactMatch>(wonderlandId, lantern2)};
    TEST(MatchResults(engine, rules, request.Results()), ());
  }
}
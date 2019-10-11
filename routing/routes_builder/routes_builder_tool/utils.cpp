#include "routing/routes_builder/routes_builder_tool/utils.hpp"

#include "routing/routing_quality/api/google/google_api.hpp"
#include "routing/routing_quality/api/mapbox/mapbox_api.hpp"

#include "routing/checkpoints.hpp"
#include "routing/vehicle_mask.hpp"

#include "platform/platform.hpp"

#include "geometry/latlon.hpp"
#include "geometry/mercator.hpp"

#include "base/assert.hpp"
#include "base/file_name_utils.hpp"
#include "base/logging.hpp"
#include "base/timer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>
#include <tuple>

#include <boost/optional.hpp>

using namespace routing_quality;

namespace
{
size_t GetNumberOfLines(std::string const & filename)
{
  std::ifstream input(filename);
  CHECK(input.good(), ("Error during opening:", filename));

  size_t count = 0;
  std::string line;
  while (std::getline(input, line))
    ++count;

  return count;
}
}  // namespace

namespace routing
{
namespace routes_builder
{
std::vector<RoutesBuilder::Result> BuildRoutes(std::string const & routesPath,
                                               std::string const & dumpPath,
                                               int64_t startFrom,
                                               uint64_t threadsNumber)
{
  CHECK(Platform::IsFileExistsByFullPath(routesPath), ("Can not find file:", routesPath));

  std::ifstream input(routesPath);
  CHECK(input.good(), ("Error during opening:", routesPath));

  std::vector<RoutesBuilder::Result> result;
  if (!threadsNumber)
  {
    auto const hardwareConcurrency = std::thread::hardware_concurrency();
    threadsNumber = hardwareConcurrency > 0 ? hardwareConcurrency : 2;
  }

  RoutesBuilder routesBuilder(threadsNumber);

  std::vector<std::future<RoutesBuilder::Result>> tasks;
  double lastPercent = 0.0;

  {
    RoutesBuilder::Params params;
    params.m_type = VehicleType::Car;

    base::ScopedLogLevelChanger changer(base::LogLevel::LERROR);
    ms::LatLon start;
    ms::LatLon finish;
    size_t startFromCopy = startFrom;
    while (input >> start.m_lat >> start.m_lon >> finish.m_lat >> finish.m_lon)
    {
      if (startFromCopy > 0)
      {
        --startFromCopy;
        continue;
      }

      auto const startPoint = MercatorBounds::FromLatLon(start);
      auto const finishPoint = MercatorBounds::FromLatLon(finish);

      params.m_checkpoints = Checkpoints(std::vector<m2::PointD>({startPoint, finishPoint}));
      tasks.emplace_back(routesBuilder.ProcessTaskAsync(params));
    }

    LOG_FORCE(LINFO, ("Created:", tasks.size(), "tasks"));
    for (size_t i = 0; i < tasks.size(); ++i)
    {
      size_t shiftIndex = i + startFrom;
      auto & task = tasks[i];
      task.wait();

      result.emplace_back(task.get());
      if (!dumpPath.empty())
      {
        std::string const fullPath =
            base::JoinPath(dumpPath, std::to_string(shiftIndex) + RoutesBuilder::Result::kDumpExtension);

        RoutesBuilder::Result::Dump(result.back(), fullPath);
      }

      double const curPercent = static_cast<double>(shiftIndex + 1) / tasks.size() * 100.0;
      if (curPercent - lastPercent > 1.0 || shiftIndex + 1 == tasks.size())
      {
        lastPercent = curPercent;
        LOG_FORCE(LINFO, ("Progress:", lastPercent, "%"));
      }
    }
  }

  return result;
}

boost::optional<std::tuple<ms::LatLon, ms::LatLon, int32_t>> ParseApiLine(std::ifstream & input)
{
  std::string line;
  if (!std::getline(input, line))
    return {};

  std::stringstream ss;
  ss << line;

  ms::LatLon start;
  ms::LatLon finish;
  ss >> start.m_lat >> start.m_lon >> finish.m_lat >> finish.m_lon;

  int32_t utcOffset = 0;
  if (!(ss >> utcOffset))
    utcOffset = 0;

  return {{start, finish, utcOffset}};
}

void BuildRoutesWithApi(std::unique_ptr<api::RoutingApi> routingApi,
                        std::string const & routesPath,
                        std::string const & dumpPath,
                        int64_t startFrom)
{
  std::ifstream input(routesPath);
  CHECK(input.good(), ("Error during opening:", routesPath));

  size_t constexpr kResponseBuffer = 50;
  std::array<api::Response, kResponseBuffer> result;

  api::Params params;
  params.m_type = api::VehicleType::Car;

  size_t rps = 0;
  base::HighResTimer timer;
  auto const getElapsedMilliSeconds = [&timer]() {
    double ms = timer.ElapsedNano() / 1e6;
    LOG(LDEBUG, ("Elapsed:", ms, "ms"));
    return ms;
  };

  auto const drop = [&]() {
    rps = 0;
    timer.Reset();
  };

  auto const sleepIfNeed = [&]() {
    double constexpr kMsInSecond = 1000.0;
    if (getElapsedMilliSeconds() > kMsInSecond)
    {
      drop();
      return;
    }

    ++rps;
    if (rps >= routingApi->GetMaxRPS())
    {
      LOG(LDEBUG, ("Sleep for 2 seconds."));
      std::this_thread::sleep_for(std::chrono::seconds(2));
      drop();
    }
  };

  size_t count = 0;
  size_t prevDumpedNumber = 0;
  auto const dump = [&]() {
    for (size_t i = 0; i < count; ++i)
    {
      std::string filepath =
          base::JoinPath(dumpPath, std::to_string(prevDumpedNumber + i) + api::Response::kDumpExtension);

      api::Response::Dump(filepath, result[i]);
    }

    prevDumpedNumber += count;
    count = 0;
  };

  size_t const tasksNumber = GetNumberOfLines(routesPath);
  LOG(LINFO, ("Receive:", tasksNumber, "for api:", routingApi->GetApiName()));
  double lastPercent = 0.0;

  ms::LatLon start;
  ms::LatLon finish;
  int32_t utcOffset;
  while (auto parsedData = ParseApiLine(input))
  {
    if (startFrom > 0)
    {
      --startFrom;
      ++count;
      if (count == kResponseBuffer)
      {
        prevDumpedNumber += count;
        count = 0;
      }

      continue;
    }

    if (count == kResponseBuffer)
      dump();

    double const curPercent = static_cast<double>(prevDumpedNumber + count) / tasksNumber * 100.0;
    if (curPercent - lastPercent > 1.0)
    {
      lastPercent = curPercent;
      LOG(LINFO, ("Progress:", lastPercent, "%"));
    }

    std::tie(start, finish, utcOffset) = *parsedData;
    params.m_waypoints = routing::Checkpoints(MercatorBounds::FromLatLon(start),
                                              MercatorBounds::FromLatLon(finish));

    result[count] = routingApi->CalculateRoute(params, utcOffset);

    sleepIfNeed();
    ++count;
  }

  dump();
}

std::unique_ptr<routing_quality::api::RoutingApi> CreateRoutingApi(std::string const & name,
                                                                   std::string const & token)
{
  if (name == "mapbox")
    return std::make_unique<routing_quality::api::mapbox::MapboxApi>(token);
  if (name == "google")
    return std::make_unique<routing_quality::api::google::GoogleApi>(token);

  LOG(LERROR, ("Unsupported api name:", name));
  UNREACHABLE();
}
}  // namespace routes_builder
}  // routing

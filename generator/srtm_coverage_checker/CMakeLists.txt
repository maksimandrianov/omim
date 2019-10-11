project(srtm_coverage_checker)

set(
  SRC
  srtm_coverage_checker.cpp
)

omim_add_executable(${PROJECT_NAME} ${SRC})

omim_link_libraries(
  ${PROJECT_NAME}
  generator
  routing
  traffic
  routing_common
  indexer
  platform
  geometry
  coding
  base
  gflags
  icu
  jansson
  minizip
  oauthcpp
  protobuf
  stats_client
)

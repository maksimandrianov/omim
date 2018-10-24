#pragma once

#include "generator/emitter_area.hpp"
#include "generator/emitter_coastline.hpp"
#include "generator/emitter_interface.hpp"
#include "generator/emitter_booking.hpp"
#include "generator/emitter_restaurants.hpp"
#include "generator/emitter_simple.hpp"
#include "generator/emitter_world.hpp"
#include "generator/factory_utils.hpp"

#include "base/assert.hpp"

#include <memory>

namespace generator
{
enum class EmitterType
{
  Restaurants,
  Simple,
  SimpleWithPreserialize,
  Area,
  Coastline,
  World
  //  Booking
};

template <class... Args>
std::shared_ptr<EmitterInterface> CreateEmitter(EmitterType type, Args&&... args)
{
  switch (type)
  {
  case EmitterType::Coastline:
    return create<EmitterCoastline>(std::forward<Args>(args)...);
  case EmitterType::Area:
    return create<EmitterArea>(std::forward<Args>(args)...);
  case EmitterType::Simple:
    return create<EmitterSimple>(std::forward<Args>(args)...);
  case EmitterType::SimpleWithPreserialize:
    return create<EmitterPreserialize>(std::forward<Args>(args)...);
  case EmitterType::Restaurants:
    return create<EmitterRestaurants>(std::forward<Args>(args)...);
  case EmitterType::World:
    return create<EmitterWorld>(std::forward<Args>(args)...);

  }
  UNREACHABLE();
}
}  // namespace generator

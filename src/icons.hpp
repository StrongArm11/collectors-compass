// The game's own Skulltula/heart map icons on the minimap (see icons.cpp).
#pragma once

#include "common.hpp"

namespace mm {

// Installs the icon-manager hook (log only); false if the patch or the game's code is not as expected.
bool installIcons();

}  // namespace mm

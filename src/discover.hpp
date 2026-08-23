#pragma once
#include <string>

namespace pt {

// Auto-locate a `progressive-cli serve --ttys` relay on 127.0.0.1. Probes
// ports in outward order from `base` (base, base+1, base-1, base+2, ... up to
// `range`), returning the first URL whose /api/ttys/sync answers like our
// server. Returns "" when nothing is found.
std::string discover_ttys_host(int base = 29325, int range = 10);

}  // namespace pt

#pragma once

// The declarations live in include/core/I18n.h, which is what the build's include path resolves.
// This file used to be a second copy of them, so a relative include from src/ picked up a stale
// interface and anything added to the real header looked missing. It now forwards instead.
#include "../../include/core/I18n.h"

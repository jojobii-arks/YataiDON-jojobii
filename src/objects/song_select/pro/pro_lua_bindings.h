#pragma once

#include "../../../libs/script.h"

// Pro select v2 — the Lua side of the list model. Registers the ProList
// usertype: the skin reads rows, resolved chart info and the view state
// through it. Values only — no userdata the skin could misuse.
void register_pro_lua_bindings(sol::state& lua);
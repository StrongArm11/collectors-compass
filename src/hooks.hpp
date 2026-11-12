// Hooks: the patch (build/<main build ID>.ips) replaces each hooked routine's first word with a branch to a gate;
// installHook checks the patch and the routine's words, then fills the gate's table slot with our handler. Calling
// the returned original runs the routine's first word and continues inside the routine.
// Inline hooks replace one instruction inside a routine with a branch to a trampoline that calls our handler and
// branches on its result (see tools/make_patch.py); installInline fills their table slots.
// Import hooks replace the first word of one of main's import stubs (calls into the system libraries) with a branch
// to a gate; installPlt fills its slot and returns the gate's copy of the stub, which calls the real import.
#pragma once

#include "common.hpp"
#include "hooks.inc"

namespace mm {

bool installHook(HookId id, void* handler, void** original);
bool installInline(InlineId id, void* handler);
bool installPlt(PltId id, void* handler, void** original);

}  // namespace mm

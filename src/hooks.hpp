// Hooks: the patch (build/<main build ID>.ips) replaces each hooked routine's first word with a branch to a gate;
// installHook checks the patch and the routine's words, then fills the gate's table slot with our handler. Calling
// the returned original runs the routine's first word and continues inside the routine.
// Inline hooks replace one instruction inside a routine with a branch to a trampoline that calls our handler and
// branches on its result (see tools/make_patch.py); installInline fills their table slots.
// Call hooks (since v1.0.1) redirect a routine's call instructions to a gate instead of patching the routine itself,
// so another mod that patches the routine's entry still runs; installCall fills the gate's slot and returns the
// routine's address (or nothing for a call through a register, whose target the gate passes to the handler).
#pragma once

#include "common.hpp"
#include "hooks.inc"

namespace mm {

bool installHook(HookId id, void* handler, void** original);
bool installInline(InlineId id, void* handler);
bool installCall(CallId id, void* handler, void** original);

}  // namespace mm

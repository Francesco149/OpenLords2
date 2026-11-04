/* rl.h — retail-layout ports outside the battlefield (family 'rl' of tools/bf/parts.py; the design is the
 * battlefield's: docs/findings/battlefield-sim-port.md). Canonical names rl_<va>; a partition's not-yet-ported
 * functions are retail stubs (src/rl_<part>_pending.c, engine_retail_call); state on retail's layout through
 * engine_bf.h's accessors (AT / REC …) and the generated globals (g_bf_<va>, g_rl_<va> per partition). */
#pragma once
#include "engine_bf.h"

#include "rl_io.h"
#include "rl_glue.h"
#include "rl_render.h"
#include "rl_misc.h"
#include "rl_skirmish.h"
#include "rl_ui2.h"
#include "rl_ui3.h"
#include "rl_ui4.h"
#include "rl_ctl.h"
#include "rl_terr.h"
#include "rl_turn.h"
#include "rl_host.h"
#include "rl_quit.h"
#include "rl_cov3.h"

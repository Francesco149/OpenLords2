/* rh_bot.c — decision-server link (bot mode). Filled in by the autoplay work (tools/bot/). */
#include "rhost.h"

void rh_bot_install(void) { if (O.bot) rh_log("bot: --bot %s not implemented yet\n", O.bot); }
void rh_bot_frame(void) {}

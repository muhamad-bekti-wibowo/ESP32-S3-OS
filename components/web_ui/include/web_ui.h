#pragma once

#include "logic_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Mulai HTTP server. prog adalah program yang akan dibaca/ditulis lewat web
 * (upload JSON baru, atau lihat status output block untuk debug). */
void web_ui_start(logic_program_t *prog);

#ifdef __cplusplus
}
#endif

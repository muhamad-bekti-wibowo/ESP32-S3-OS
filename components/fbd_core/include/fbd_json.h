#pragma once

#include <stdbool.h>
#include "cJSON.h"
#include "fbd_graph.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FBD_JSON_ERR_LEN 128

/* Parse JSON (root object) ke *out_graph. TIDAK memanggil fbd_graph_compile()
 * - panggil itu sendiri setelah parse sukses. Return false kalau JSON tidak
 * valid sesuai schema.md - err diisi pesan singkat kalau gagal (boleh NULL
 * kalau tidak butuh pesan).
 *
 * PENTING: *out_graph ditulis LANGSUNG selama parsing (bukan lewat local
 * variable perantara di stack - sizeof(fbd_graph_t) ~19KB, terlalu besar
 * untuk stack task manapun yang wajar dan pernah menyebabkan stack overflow
 * yang merusak heap secara diam-diam). Konsekuensinya: kalau fbd_json_parse()
 * return false, *out_graph BISA dalam keadaan tidak lengkap/partial - caller
 * TIDAK BOLEH memakainya, hanya boleh diabaikan atau di-fbd_graph_init()
 * ulang. Ini artinya *out_graph HARUS berupa storage yang aman ditimpa
 * sementara (misal scratch buffer statis terpisah dari graph aktif yang
 * sedang jalan), BUKAN langsung graph yang lagi dieksekusi scan task. */
bool fbd_json_parse(const cJSON *root, fbd_graph_t *out_graph, char *err, size_t err_len);

/* Serialize graph ke cJSON object baru (caller yang men-cJSON_Delete()). */
cJSON *fbd_json_serialize(const fbd_graph_t *g);

#ifdef __cplusplus
}
#endif

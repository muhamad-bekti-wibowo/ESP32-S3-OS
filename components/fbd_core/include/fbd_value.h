#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FBD_EMPTY = 0,
    FBD_BOOL,
    FBD_INT32,
    FBD_FLOAT,
    FBD_BYTES   /* payload register I2C kecil, maks 8 byte */
} fbd_type_t;

typedef struct {
    fbd_type_t type;
    union {
        bool b;
        int32_t i;
        float f;
        struct { uint8_t data[8]; uint8_t len; } bytes;
    };
} fbd_value_t;

fbd_value_t fbd_make_empty(void);
fbd_value_t fbd_make_bool(bool v);
fbd_value_t fbd_make_int(int32_t v);
fbd_value_t fbd_make_float(float v);
fbd_value_t fbd_make_bytes(const uint8_t *src, uint8_t len);

/* Auto-convert BOOL/INT32 -> float */
float fbd_to_float(fbd_value_t v);
/* Auto-convert INT32/FLOAT -> bool */
bool  fbd_to_bool(fbd_value_t v);

#ifdef __cplusplus
}
#endif

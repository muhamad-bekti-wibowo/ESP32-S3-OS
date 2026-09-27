#include "fbd_value.h"
#include <string.h>

fbd_value_t fbd_make_empty(void)
{
    fbd_value_t v;
    memset(&v, 0, sizeof(v));
    v.type = FBD_EMPTY;
    return v;
}

fbd_value_t fbd_make_bool(bool b)
{
    fbd_value_t v;
    memset(&v, 0, sizeof(v));
    v.type = FBD_BOOL;
    v.b = b;
    return v;
}

fbd_value_t fbd_make_int(int32_t i)
{
    fbd_value_t v;
    memset(&v, 0, sizeof(v));
    v.type = FBD_INT32;
    v.i = i;
    return v;
}

fbd_value_t fbd_make_float(float f)
{
    fbd_value_t v;
    memset(&v, 0, sizeof(v));
    v.type = FBD_FLOAT;
    v.f = f;
    return v;
}

fbd_value_t fbd_make_bytes(const uint8_t *src, uint8_t len)
{
    fbd_value_t v;
    memset(&v, 0, sizeof(v));
    v.type = FBD_BYTES;
    if (len > sizeof(v.bytes.data)) {
        len = sizeof(v.bytes.data);
    }
    memcpy(v.bytes.data, src, len);
    v.bytes.len = len;
    return v;
}

float fbd_to_float(fbd_value_t v)
{
    switch (v.type) {
        case FBD_BOOL:  return v.b ? 1.0f : 0.0f;
        case FBD_INT32: return (float)v.i;
        case FBD_FLOAT: return v.f;
        default:        return 0.0f;
    }
}

bool fbd_to_bool(fbd_value_t v)
{
    switch (v.type) {
        case FBD_BOOL:  return v.b;
        case FBD_INT32: return v.i != 0;
        case FBD_FLOAT: return v.f != 0.0f;
        default:        return false;
    }
}

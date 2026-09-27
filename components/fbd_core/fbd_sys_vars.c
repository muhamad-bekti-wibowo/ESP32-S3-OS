#include "fbd_sys_vars.h"
#include <stddef.h>

static fbd_sys_var_provider_fn s_provider = NULL;

void fbd_sys_vars_set_provider(fbd_sys_var_provider_fn provider)
{
    s_provider = provider;
}

fbd_value_t fbd_sys_vars_get(const char *name)
{
    if (!s_provider) {
        return fbd_make_empty();
    }
    return s_provider(name);
}

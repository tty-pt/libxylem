#include <node_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ttypt/xy.h>

typedef struct {
    char name[64];
    uint32_t hook_id;
} XylemHookEntry;

#define MAX_NAPI_HOOKS 128
static XylemHookEntry g_napi_hooks[MAX_NAPI_HOOKS];
static size_t g_napi_hook_count = 0;

static napi_value XylemRegisterHook(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);

    char name_buf[64] = {0};
    size_t len = 0;
    napi_get_value_string_utf8(env, args[0], name_buf, sizeof(name_buf) - 1, &len);

    if (g_napi_hook_count >= MAX_NAPI_HOOKS) {
        napi_throw_error(env, NULL, "Xylem hook table full");
        return NULL;
    }

    uint32_t id = (uint32_t)g_napi_hook_count++;
    strncpy(g_napi_hooks[id].name, name_buf, sizeof(g_napi_hooks[id].name) - 1);
    g_napi_hooks[id].hook_id = id;

    napi_value res;
    napi_create_uint32(env, id, &res);
    return res;
}

static napi_value XylemCallHook(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);

    char name_buf[64] = {0};
    size_t len = 0;
    napi_get_value_string_utf8(env, args[0], name_buf, sizeof(name_buf) - 1, &len);

    int found_id = -1;
    for (size_t i = 0; i < g_napi_hook_count; i++) {
        if (strcmp(g_napi_hooks[i].name, name_buf) == 0) {
            found_id = (int)g_napi_hooks[i].hook_id;
            break;
        }
    }

    napi_value res;
    napi_create_int32(env, found_id, &res);
    return res;
}

static napi_value XylemVersion(napi_env env, napi_callback_info info) {
    (void)info;
    napi_value res;
    napi_create_string_utf8(env, "0.1.0", NAPI_AUTO_LENGTH, &res);
    return res;
}

static napi_value Init(napi_env env, napi_value exports) {
    napi_property_descriptor desc[] = {
        {"registerHook", NULL, XylemRegisterHook, NULL, NULL, NULL, napi_default, NULL},
        {"callHook", NULL, XylemCallHook, NULL, NULL, NULL, napi_default, NULL},
        {"version", NULL, XylemVersion, NULL, NULL, NULL, napi_default, NULL}
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)

#include "js_native_api_types.h"
#include <mqueue.h>

char *get_string(napi_env env, napi_value string);
void handle_error(napi_env env);
void handle_cerror(napi_env env);
napi_value open_posix_mq(napi_env env, napi_callback_info info);
napi_value close_posix_mq(napi_env env, napi_callback_info info);
napi_value send_posix_mq(napi_env env, napi_callback_info info);
napi_value receive_posix_mq(napi_env env, napi_callback_info info);
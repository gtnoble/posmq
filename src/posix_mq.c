#include "js_native_api.h"
#include "js_native_api_types.h"
#include <bits/types/sigevent_t.h>
#include <fcntl.h>
#include <node_api.h>
#include <mqueue.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include "posix_mq.h"
#include "errnoname.h"

#define HANDLE_ERROR(status) if (status != napi_ok) {handle_error(env); return NULL;}

napi_status get_string(napi_env env, napi_value node_string, char **string) {

  size_t string_length;
  napi_status status = napi_get_value_string_utf8(
    env, node_string, NULL, 0, &string_length
  );
  if (status != napi_ok) {
    return status;
  }

  size_t buffer_size = string_length + 1;
  char *c_string = calloc(buffer_size, sizeof(char));

  status = napi_get_value_string_utf8(
    env, node_string, c_string, buffer_size, NULL
  );

  if (status != napi_ok) {
    free(c_string);
    *string = NULL;
    handle_error(env);
    return status;
  }
  
  *string = c_string;

  return status;
}

void handle_error(napi_env env) {
  const napi_extended_error_info* error_info;
  napi_get_last_error_info(env, &error_info);
  const char *error_message = error_info->error_message;
  napi_throw_error(env, NULL, error_message);
}

napi_value cerror_name(napi_env env) {
  const char *error_name = errnoname(errno);
  napi_value node_error_string;
  napi_status status = napi_create_string_utf8(
    env, 
    error_name, 
    NAPI_AUTO_LENGTH, 
    &node_error_string
  );
  if (status != napi_ok) {
    handle_error(env);
    return NULL;
  }
  return node_error_string;
}

napi_value get_message_queue_attributes(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value function_argv[1];
  napi_status status = napi_get_cb_info(env, info, &argc, function_argv, NULL, NULL);
  HANDLE_ERROR(status)
  
  struct mq_attr mq_attributes;
  int32_t mq_descriptor;
  
  status = napi_get_value_int32(env, function_argv[0], &mq_descriptor);
  HANDLE_ERROR(status)
  
  if (mq_getattr(mq_descriptor, &mq_attributes)) {
    return cerror_name(env);
  }

  napi_value message_queue_flags;
  status = napi_create_int64(env, mq_attributes.mq_flags, &message_queue_flags);
  HANDLE_ERROR(status)
  
  napi_value message_queue_max_messages;
  status = napi_create_int64(env, mq_attributes.mq_maxmsg, &message_queue_max_messages);
  HANDLE_ERROR(status)
  
  napi_value message_queue_max_message_size;
  status = napi_create_int64(env, mq_attributes.mq_msgsize, &message_queue_max_message_size);
  HANDLE_ERROR(status)
  
  napi_value message_queue_current_message_count;
  status = napi_create_int64(env, mq_attributes.mq_curmsgs, &message_queue_current_message_count);
  HANDLE_ERROR(status)
  
  napi_value node_mq_attributes_object;
  status = napi_create_object(env, &node_mq_attributes_object);
  HANDLE_ERROR(status)
  status = napi_set_named_property(env, node_mq_attributes_object, "flags", message_queue_flags);
  HANDLE_ERROR(status)
  status = napi_set_named_property(env, node_mq_attributes_object, "maxMessages", message_queue_max_messages);
  HANDLE_ERROR(status)
  status = napi_set_named_property(env, node_mq_attributes_object, "maxMessageSize", message_queue_max_message_size);
  HANDLE_ERROR(status)
  status = napi_set_named_property(env, node_mq_attributes_object, "currentMessageCount", message_queue_current_message_count);
  HANDLE_ERROR(status)
    
  return node_mq_attributes_object;
}

napi_status check_if_undefined(napi_env env, napi_value value, bool *is_undefined) {
  napi_valuetype type;
  napi_status status = napi_typeof(env, value, &type);
  if (type == napi_undefined) {
    *is_undefined = true;
  }
  else {
    *is_undefined = false;
  }
  return status;
}

napi_value open_posix_mq(napi_env env, napi_callback_info info) {
  size_t argc = 4;
  napi_value function_argv[4];
  napi_status status = napi_get_cb_info(env, info, &argc, function_argv, NULL, NULL);
  HANDLE_ERROR(status)

  napi_value name = function_argv[0];
  char *c_name;
  status = get_string(env, name, &c_name);
  HANDLE_ERROR(status)

  int32_t oflag;
  status = napi_get_value_int32(env, function_argv[1], &oflag);
  HANDLE_ERROR(status)

  uint32_t max_msgs; 
  status = napi_get_value_uint32(env, function_argv[2], &max_msgs);
  HANDLE_ERROR(status)
  
  uint32_t max_msg_size;
  status = napi_get_value_uint32(env, function_argv[3], &max_msg_size);
  HANDLE_ERROR(status)

  struct mq_attr attributes = {
    .mq_maxmsg = max_msgs,
    .mq_msgsize = max_msg_size
  };
  int32_t mq = mq_open(
      c_name,
      oflag, 
      S_IRWXU, 
      &attributes
  );
  free(c_name);
  
  if (mq == (mqd_t) -1) {
    return cerror_name(env);
  }
  
  napi_value message_descriptor;
  status = napi_create_int32(env, mq, &message_descriptor);
  HANDLE_ERROR(status)
  return message_descriptor;
}

napi_value close_posix_mq(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  napi_status status = napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
  HANDLE_ERROR(status)

  int32_t message_descriptor;
  status = napi_get_value_int32(env, argv[0], &message_descriptor);
  if (mq_close(message_descriptor)) {
    return cerror_name(env);
  }
  return NULL;
}

napi_status get_timespec(napi_env env, napi_value timespec_object, struct timespec *time) {
    napi_value seconds;
    napi_status status = napi_get_named_property(
      env, timespec_object, "seconds", &seconds
    );
    if (status != napi_ok)
      return status;

    napi_value nanoseconds;
    status = napi_get_named_property(
      env, timespec_object, "nanoseconds", &nanoseconds
    );
    if (status != napi_ok)
      return status;

    int64_t seconds_value;
    status = napi_get_value_int64(env, seconds, &seconds_value);
    if (status != napi_ok)
      return status;
    
    int64_t nanoseconds_value;
    status = napi_get_value_int64(env, nanoseconds, &nanoseconds_value);
    if (status != napi_ok)
      return status;
      
    time->tv_sec = seconds_value;
    time->tv_nsec = nanoseconds_value;
    
    return napi_ok;
}

napi_value send_posix_mq(napi_env env, napi_callback_info info) {
  size_t argc = 4;
  napi_value argv[4];
  napi_status status = napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
  HANDLE_ERROR(status)

  napi_value undefined;
  napi_get_undefined(env, &undefined);
  
  napi_value message_queue_object = argv[0];
  int32_t message_descriptor; 
  status = napi_get_value_int32(env, message_queue_object, &message_descriptor);

  
  napi_value message = argv[1];
  char *message_data;
  size_t message_length;
  status = napi_get_buffer_info(
    env, message, (void **)&message_data, &message_length
  );
  HANDLE_ERROR(status)

  uint32_t message_priority;
  status = napi_get_value_uint32(env, argv[2], &message_priority);
  HANDLE_ERROR(status)
    
  napi_value absolute_timeout = argv[3];
  
  bool timeout_is_undefined;
  check_if_undefined(env, absolute_timeout, &timeout_is_undefined);
  HANDLE_ERROR(status)

  if (! timeout_is_undefined) {
    struct timespec c_timeout;
    status = get_timespec(env, absolute_timeout, &c_timeout);
    HANDLE_ERROR(status)
      
    if (mq_timedsend(
      message_descriptor, 
      message_data, 
      message_length, 
      message_priority, 
      &c_timeout)) {
      return cerror_name(env);
    }
    return NULL;
  }
  

  if (mq_send(
    message_descriptor,
    message_data, 
    message_length, 
    message_priority
  )) {
    return cerror_name(env);
  }
  return NULL;
}

napi_value receive_posix_mq(napi_env env, napi_callback_info info) {
  size_t argc = 3;
  napi_value argv[3];
  napi_status status = napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
  HANDLE_ERROR(status)

  napi_value undefined;
  napi_get_undefined(env, &undefined);
  
  int32_t mq;
  status = napi_get_value_int32(env, argv[0], &mq);
  HANDLE_ERROR(status)

  uint32_t message_length;
  status = napi_get_value_uint32(env, argv[1], &message_length);
  HANDLE_ERROR(status)
    
  char *message_data;
  unsigned int priority;

  napi_value absolute_timeout = argv[2];
  bool timeout_is_undefined;
  check_if_undefined(env, absolute_timeout, &timeout_is_undefined);
  HANDLE_ERROR(status)

  if (! timeout_is_undefined) {
    struct timespec timeout;
    status = get_timespec(env, absolute_timeout, &timeout);
    HANDLE_ERROR(status)
      
    message_data =  malloc(sizeof(char) * message_length);

    if (mq_timedreceive(
      mq, 
      message_data, 
      message_length, 
      &priority, 
      &timeout)) {
        free(message_data);
        return cerror_name(env);
      }
  }
    
  else {
    message_data =  malloc(sizeof(char) * message_length);

    if (mq_receive(mq, message_data, message_length, &priority) == -1) {
      free(message_data);
      return cerror_name(env);
    }
  }
  
  napi_value node_data;
  status = napi_create_buffer_copy(env, message_length, message_data, NULL, &node_data);
  free(message_data);
  HANDLE_ERROR(status)
  return node_data;
}

napi_value notify_posix_mq(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  napi_status status = napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
  HANDLE_ERROR(status)
    
  int32_t mq;
  status = napi_get_value_int32(env, argv[0], &mq);
  HANDLE_ERROR(status)
    
  struct sigevent notify_event = {
    .sigev_notify = SIGEV_SIGNAL,
    .sigev_signo = SIGUSR2
  };
  
  if (mq_notify(mq, &notify_event)) {
    return cerror_name(env);
  }
  else {
    return NULL;
  }
  
}

napi_value unlink_posix_mq(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  napi_status status = napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
  HANDLE_ERROR(status)
    
  char *mq_name;
  status = get_string(env, argv[0], &mq_name);
  HANDLE_ERROR(status);
  
  if(mq_unlink(mq_name)) {
    return cerror_name(env);
  }
  return NULL;
}

NAPI_MODULE_INIT() {
  napi_value mq_unlink_fn;
  napi_create_function(
    env, 
    "posixMqUnlink", 
    NAPI_AUTO_LENGTH, 
    unlink_posix_mq, 
    NULL, 
    &mq_unlink_fn
  );
  napi_set_named_property(env, exports, "posixMqUnlink", mq_unlink_fn);

  napi_value mq_notify_fn;
  napi_create_function(
    env, 
    "posixMqNotify", 
    NAPI_AUTO_LENGTH, 
    get_message_queue_attributes, 
    NULL, 
    &mq_notify_fn
  );
  napi_set_named_property(env, exports, "posixMqNotify", mq_notify_fn);

  napi_value mq_attributes_fn;
  napi_create_function(
    env, 
    "posixMqAttributes", 
    NAPI_AUTO_LENGTH, 
    get_message_queue_attributes, 
    NULL, 
    &mq_attributes_fn
  );
  napi_set_named_property(env, exports, "posixMqAttributes", mq_attributes_fn);

  napi_value open_mq_fn;
  napi_create_function(
    env, 
    "openPosixMq", 
    NAPI_AUTO_LENGTH, 
    open_posix_mq, 
    NULL, 
    &open_mq_fn
  );
  napi_set_named_property(env, exports, "openPosixMq", open_mq_fn);
  
  napi_value close_mq_fn;
  napi_create_function(
    env, 
    "closePosixMq", 
    NAPI_AUTO_LENGTH, 
    close_posix_mq, 
    NULL, 
    &close_mq_fn
  );
  napi_set_named_property(env, exports, "closePosixMq", close_mq_fn);
  
  napi_value send_mq_fn;
  napi_create_function(
    env, 
    "sendPosixMq", 
    NAPI_AUTO_LENGTH, 
    send_posix_mq, 
    NULL, 
    &send_mq_fn
  );
  napi_set_named_property(env, exports, "sendPosixMq", send_mq_fn);
  
  napi_value receive_mq_fn;
  napi_create_function(
    env, 
    "receivePosixMq", 
    NAPI_AUTO_LENGTH, 
    receive_posix_mq, 
    NULL, 
    &receive_mq_fn
  );
  napi_set_named_property(env, exports, "receivePosixMq", receive_mq_fn);
  
  return exports;
}
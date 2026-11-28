#pragma once
#include <stdint.h>

#ifdef _WIN32
  #ifdef ATMERA_PLUGIN_EXPORT
    #define ATMERA_PLUGIN_API __declspec(dllexport)
  #else
    #define ATMERA_PLUGIN_API
  #endif
#else
  #define ATMERA_PLUGIN_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define ATMERA_PLUGIN_ABI_VERSION 1u
#define ATMERA_PLUGIN_ENTRY "atmera_get_plugin_api"

typedef void* atmera_plugin_handle;
typedef void (*atmera_log_fn)(void* context, uint32_t level, const char* message);
typedef void (*atmera_event_fn)(void* user_context, const char* payload);
typedef uint32_t (*atmera_register_service_fn)(void* context, const char* id, void* instance);
typedef void* (*atmera_find_service_fn)(void* context, const char* id);
typedef uint64_t (*atmera_subscribe_event_fn)(void* context, const char* name,
    atmera_event_fn callback, void* user_context);
typedef void (*atmera_unsubscribe_event_fn)(void* context, uint64_t token);
typedef void (*atmera_publish_event_fn)(void* context, const char* name, const char* payload);

typedef struct atmera_host_api {
    uint32_t struct_size;
    uint32_t abi_version;
    void* context;
    atmera_log_fn log;
    // Append-only extensions. Plugins must check struct_size before using these fields.
    atmera_register_service_fn register_service;
    atmera_find_service_fn find_service;
    atmera_subscribe_event_fn subscribe_event;
    atmera_unsubscribe_event_fn unsubscribe_event;
    atmera_publish_event_fn publish_event;
} atmera_host_api;

typedef struct atmera_plugin_api {
    uint32_t struct_size;
    uint32_t abi_version;
    const char* module_id;
    uint32_t (*initialize)(const atmera_host_api* host, atmera_plugin_handle* out_handle);
    void (*shutdown)(atmera_plugin_handle handle);
} atmera_plugin_api;

// The returned table and module_id remain owned by the DLL until it is unloaded.
// A nonzero result indicates failure; no C++ exceptions may cross this boundary.
typedef uint32_t (*atmera_get_plugin_api_fn)(uint32_t host_abi_version, const atmera_plugin_api** out_api);
ATMERA_PLUGIN_API uint32_t atmera_get_plugin_api(uint32_t host_abi_version, const atmera_plugin_api** out_api);

#ifdef __cplusplus
}
#endif

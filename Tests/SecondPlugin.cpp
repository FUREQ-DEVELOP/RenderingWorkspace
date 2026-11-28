#define ATMERA_PLUGIN_EXPORT
#include <ATMERA/PluginABI.h>
#include <windows.h>
#include <new>

namespace {
struct State { void* context; atmera_log_fn log; };
uint32_t initialize(const atmera_host_api* host, atmera_plugin_handle* out_handle) {
    if (!host || host->struct_size < sizeof(atmera_host_api) ||
        host->abi_version != ATMERA_PLUGIN_ABI_VERSION || !out_handle) return 1;
    if (GetEnvironmentVariableW(L"ATMERA_TEST_FAIL_SECOND", nullptr, 0) != 0) return 3;
    if (!host->find_service || !host->find_service(host->context, "atmera.test.state")) return 4;
    *out_handle = new (std::nothrow) State{host->context, host->log};
    if (!*out_handle) return 2;
    if (host->publish_event) host->publish_event(host->context, "atmera.ping", "hello");
    if (host->log) host->log(host->context, 1, "atmera.second initialized");
    return 0;
}
void shutdown(atmera_plugin_handle handle) {
    auto* state = static_cast<State*>(handle);
    if (state->log) state->log(state->context, 1, "atmera.second shutdown");
    delete state;
}
const atmera_plugin_api api{sizeof(atmera_plugin_api), ATMERA_PLUGIN_ABI_VERSION,
    "atmera.second", &initialize, &shutdown};
}
extern "C" ATMERA_PLUGIN_API uint32_t atmera_get_plugin_api(uint32_t host_abi_version,
    const atmera_plugin_api** out_api) {
    if (!out_api || host_abi_version != ATMERA_PLUGIN_ABI_VERSION) return 1;
    *out_api = &api;
    return 0;
}

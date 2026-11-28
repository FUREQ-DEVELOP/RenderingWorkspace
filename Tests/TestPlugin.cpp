#define ATMERA_PLUGIN_EXPORT
#include <ATMERA/PluginABI.h>
#include <windows.h>
#include <new>

namespace {
struct State { void* context; atmera_log_fn log; };
void on_ping(void* context, const char* payload) {
    auto* state = static_cast<State*>(context);
    if (state && state->log && payload)
        state->log(state->context, 1, "atmera.test received ping");
}
uint32_t initialize(const atmera_host_api* host, atmera_plugin_handle* out_handle) {
    if (!host || host->struct_size < sizeof(atmera_host_api) ||
        host->abi_version != ATMERA_PLUGIN_ABI_VERSION || !out_handle) return 1;
    if (GetEnvironmentVariableW(L"ATMERA_TEST_FAIL_INIT", nullptr, 0) != 0) {
        if (GetEnvironmentVariableW(L"ATMERA_TEST_FAIL_ONCE", nullptr, 0) != 0) {
            SetEnvironmentVariableW(L"ATMERA_TEST_FAIL_INIT", nullptr);
            SetEnvironmentVariableW(L"ATMERA_TEST_FAIL_ONCE", nullptr);
        }
        return 3;
    }
    *out_handle = new (std::nothrow) State{host->context, host->log};
    if (!*out_handle) return 2;
    if (!host->register_service || host->register_service(host->context, "atmera.test.state", *out_handle) != 0 ||
        !host->subscribe_event || host->subscribe_event(host->context, "atmera.ping", &on_ping, *out_handle) == 0) {
        delete static_cast<State*>(*out_handle);
        *out_handle = nullptr;
        return 4;
    }
    if (host->log) host->log(host->context, 1, "Test plugin initialized");
    return 0;
}
void shutdown(atmera_plugin_handle handle) {
    auto* state = static_cast<State*>(handle);
    if (state->log) state->log(state->context, 1, "atmera.test shutdown");
    delete state;
}
const atmera_plugin_api api{sizeof(atmera_plugin_api), ATMERA_PLUGIN_ABI_VERSION,
    "atmera.test", &initialize, &shutdown};
}
extern "C" ATMERA_PLUGIN_API uint32_t atmera_get_plugin_api(uint32_t host_abi_version,
    const atmera_plugin_api** out_api) {
    if (!out_api || host_abi_version != ATMERA_PLUGIN_ABI_VERSION) return 1;
    *out_api = &api;
    return 0;
}

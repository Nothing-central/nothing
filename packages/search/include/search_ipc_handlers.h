#pragma once
#include "search_engine_manager.h"
#include <string>

// Bridges SearchEngineManager to piggy's existing named-pipe IPC dispatch —
// same transport PiggyFind/PiggyProvide/etc already use, no new protocol.
// NOTE: the exact registration call (whatever function piggycpp's dispatcher
// uses to bind a command string to a handler) is a placeholder here —
// wire RegisterSearchHandlers's body to match that once confirmed.
struct IpcRequest {
    std::string command;
    std::string paramsJson; // raw JSON object as string, parsed per-handler
};

struct IpcResponse {
    bool ok = false;
    std::string resultJson; // raw JSON to send back, "{}" on failure with no data
    std::string error;      // human-readable error, empty on success
};

class SearchIpcHandlers {
public:
    explicit SearchIpcHandlers(SearchEngineManager& manager) : manager_(manager) {}

    // Dispatch entry point — piggycpp's IPC server calls this for any
    // command starting with "search.". Returns false if the command isn't
    // one of this module's, so the caller can fall through to other handlers.
    bool TryHandle(const IpcRequest& req, IpcResponse& outResp);

private:
    SearchEngineManager& manager_;

    IpcResponse HandleSetEngine(const std::string& paramsJson);
    IpcResponse HandleGetEngine(const std::string& paramsJson);
    IpcResponse HandleListEngines();
    IpcResponse HandleRegisterCustom(const std::string& paramsJson);
    IpcResponse HandleBuildQueryUrl(const std::string& paramsJson);
};

// Call once at startup, wherever piggycpp's other Piggy* handlers are
// registered (e.g. alongside PiggyFind/PiggyProvide registration).
void RegisterSearchHandlers(SearchIpcHandlers& handlers /*, IpcDispatcher& dispatcher */);
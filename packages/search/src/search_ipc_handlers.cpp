#include "search_ipc_handlers.h"
#include <sstream>

// Minimal ad-hoc JSON extraction — replace with piggycpp's existing JSON
// lib (whatever PiggyProvide/PiggyDialog already use) once confirmed,
// rather than hand-rolling parsing here long-term.
namespace {
std::string ExtractStringField(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\":\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return "";
    pos += pattern.size();
    size_t end = json.find("\"", pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}
}

bool SearchIpcHandlers::TryHandle(const IpcRequest& req, IpcResponse& outResp) {
    if (req.command == "search.setEngine") {
        outResp = HandleSetEngine(req.paramsJson);
    } else if (req.command == "search.getEngine") {
        outResp = HandleGetEngine(req.paramsJson);
    } else if (req.command == "search.listEngines") {
        outResp = HandleListEngines();
    } else if (req.command == "search.registerCustom") {
        outResp = HandleRegisterCustom(req.paramsJson);
    } else if (req.command == "search.buildQueryUrl") {
        outResp = HandleBuildQueryUrl(req.paramsJson);
    } else {
        return false; // not ours — let the dispatcher try other handlers
    }
    return true;
}

IpcResponse SearchIpcHandlers::HandleSetEngine(const std::string& paramsJson) {
    std::string contextId = ExtractStringField(paramsJson, "contextId");
    std::string engineId = ExtractStringField(paramsJson, "engineId");
    if (contextId.empty()) contextId = "default";

    IpcResponse resp;
    resp.ok = manager_.SetActiveEngine(contextId, engineId);
    resp.resultJson = "{}";
    if (!resp.ok) resp.error = "Unknown search engine: " + engineId;
    return resp;
}

IpcResponse SearchIpcHandlers::HandleGetEngine(const std::string& paramsJson) {
    std::string contextId = ExtractStringField(paramsJson, "contextId");
    if (contextId.empty()) contextId = "default";

    const SearchEngine& e = manager_.GetActiveEngine(contextId);
    std::ostringstream json;
    json << "{\"id\":\"" << e.id << "\",\"displayName\":\"" << e.displayName
         << "\",\"homeUrl\":\"" << e.homeUrl << "\",\"requiresTor\":"
         << (e.requiresTor ? "true" : "false") << "}";

    IpcResponse resp;
    resp.ok = true;
    resp.resultJson = json.str();
    return resp;
}

IpcResponse SearchIpcHandlers::HandleListEngines() {
    std::ostringstream json;
    json << "{\"engines\":[";
    auto ids = manager_.Registry().ListIds();
    for (size_t i = 0; i < ids.size(); ++i) {
        const SearchEngine* e = manager_.Registry().Get(ids[i]);
        json << "{\"id\":\"" << e->id << "\",\"displayName\":\"" << e->displayName
             << "\",\"requiresTor\":" << (e->requiresTor ? "true" : "false")
             << ",\"isCustom\":" << (e->isCustom ? "true" : "false") << "}";
        if (i + 1 < ids.size()) json << ",";
    }
    json << "]}";

    IpcResponse resp;
    resp.ok = true;
    resp.resultJson = json.str();
    return resp;
}

IpcResponse SearchIpcHandlers::HandleRegisterCustom(const std::string& paramsJson) {
    std::string id = ExtractStringField(paramsJson, "id");
    std::string displayName = ExtractStringField(paramsJson, "displayName");
    std::string baseUrl = ExtractStringField(paramsJson, "baseUrl");

    IpcResponse resp;
    if (id.empty() || baseUrl.empty()) {
        resp.ok = false;
        resp.error = "id and baseUrl are required";
        resp.resultJson = "{}";
        return resp;
    }

    const_cast<SearchEngineRegistry&>(manager_.Registry())
        .RegisterCustom(id, displayName.empty() ? id : displayName, baseUrl);
    resp.ok = true;
    resp.resultJson = "{}";
    return resp;
}

IpcResponse SearchIpcHandlers::HandleBuildQueryUrl(const std::string& paramsJson) {
    std::string contextId = ExtractStringField(paramsJson, "contextId");
    std::string query = ExtractStringField(paramsJson, "query");
    if (contextId.empty()) contextId = "default";

    const SearchEngine& e = manager_.GetActiveEngine(contextId);
    std::string url = e.BuildQueryUrl(query); // NOTE: query should already be
                                               // URL-encoded by the caller —
                                               // encoding not done here.

    std::ostringstream json;
    json << "{\"url\":\"" << url << "\"}";

    IpcResponse resp;
    resp.ok = true;
    resp.resultJson = json.str();
    return resp;
}

void RegisterSearchHandlers(SearchIpcHandlers& handlers /*, IpcDispatcher& dispatcher */) {
    // Placeholder — wire this to piggycpp's actual dispatcher registration
    // once its exact API is confirmed (e.g. dispatcher.Register("search.*",
    // [&handlers](...) { ... }), matching however PiggyFind/PiggyProvide
    // register themselves today).
}
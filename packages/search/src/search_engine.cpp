#include "search_engine.h"

namespace {
std::string ReplacePlaceholder(std::string tmpl, const std::string& value) {
    const std::string placeholder = "{q}";
    size_t pos = tmpl.find(placeholder);
    if (pos != std::string::npos) {
        tmpl.replace(pos, placeholder.size(), value);
    }
    return tmpl;
}
}

std::string SearchEngine::BuildQueryUrl(const std::string& urlEncodedQuery) const {
    return ReplacePlaceholder(queryUrlTemplate, urlEncodedQuery);
}

std::string SearchEngine::BuildSuggestUrl(const std::string& urlEncodedQuery) const {
    if (suggestUrlTemplate.empty()) return "";
    return ReplacePlaceholder(suggestUrlTemplate, urlEncodedQuery);
}
// ═══════════════════════════════════════════ SearchController.cpp ═════════════════════════════════════
#include "SearchController.h"
#include <QUrl>

SearchController::SearchController(QObject* parent)
    : QObject(parent), manager_(registry_) {}

QStringList SearchController::engineIds() const {
    QStringList ids;
    for (const auto& id : registry_.ListIds())
        ids << QString::fromStdString(id);
    return ids;
}

QString SearchController::engineDisplayName(const QString& id) const {
    const SearchEngine* e = registry_.Get(id.toStdString());
    return e ? QString::fromStdString(e->displayName) : id;
}

QString SearchController::currentEngineId(const QString& contextId) const {
    return QString::fromStdString(manager_.GetActiveEngine(contextId.toStdString()).id);
}

bool SearchController::setEngine(const QString& contextId, const QString& engineId) {
    bool ok = manager_.SetActiveEngine(contextId.toStdString(), engineId.toStdString());
    if (ok) emit engineChanged(contextId);
    return ok;
}

QString SearchController::buildQueryUrl(const QString& contextId, const QString& query) const {
    const SearchEngine& e = manager_.GetActiveEngine(contextId.toStdString());
    std::string encoded = QUrl::toPercentEncoding(query).toStdString();
    return QString::fromStdString(e.BuildQueryUrl(encoded));
}
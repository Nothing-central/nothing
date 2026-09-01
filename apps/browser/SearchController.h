// ═══════════════════════════════════════════ SearchController.h ═══════════════════════════════════════
#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include "search_engine_registry.h"
#include "search_engine_manager.h"

class SearchController : public QObject {
    Q_OBJECT
public:
    explicit SearchController(QObject* parent = nullptr);

    Q_INVOKABLE QStringList engineIds() const;
    Q_INVOKABLE QString engineDisplayName(const QString& id) const;
    Q_INVOKABLE QString currentEngineId(const QString& contextId) const;
    Q_INVOKABLE bool setEngine(const QString& contextId, const QString& engineId);
    Q_INVOKABLE QString buildQueryUrl(const QString& contextId, const QString& query) const;

signals:
    void engineChanged(const QString& contextId);

private:
    SearchEngineRegistry registry_;   // must be declared before manager_
    SearchEngineManager  manager_;
};
#pragma once
#include <QWebEngineUrlSchemeHandler>
#include "FossilCacheManager.h"

// Intercepts sabre-fossil:// URLs and serves data from SQLite
class FossilSchemeHandler : public QWebEngineUrlSchemeHandler {
    Q_OBJECT
public:
    explicit FossilSchemeHandler(FossilCacheManager* manager, QObject* parent = nullptr);
    void requestStarted(QWebEngineUrlRequestJob* job) override;

private:
    FossilCacheManager* m_manager;
};

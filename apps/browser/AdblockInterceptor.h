#pragma once
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineUrlRequestInfo>
#include "blocker.h"
#include "request.h"

class AdblockInterceptor : public QWebEngineUrlRequestInterceptor {
    Q_OBJECT
public:
    explicit AdblockInterceptor(QObject* parent = nullptr);
    void interceptRequest(QWebEngineUrlRequestInfo& info) override;

private:
    adblock::Blocker m_blocker;
    adblock::RequestType mapResourceType(QWebEngineUrlRequestInfo::ResourceType type) const;
};

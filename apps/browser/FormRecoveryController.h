// ═══════════════════════════════════════════ FormRecoveryController.h ═══════════════════════════════════════════
#pragma once
#include <QObject>
#include <QString>
#include <QSqlDatabase>
#include <QJSValue>

class FormRecoveryController : public QObject {
    Q_OBJECT
public:
    static FormRecoveryController* instance();
    ~FormRecoveryController();

    void initialize();

public slots:
    void saveForm(const QString& pageUrl, const QString& formId, const QString& jsonData);
    void getFormsForPage(const QString& pageUrl, const QJSValue& callback);
    void markPendingSubmit(const QString& pageUrl, const QString& formId);
    void hasPendingSubmit(const QString& pageUrl, const QString& formId, const QJSValue& callback);
    void clearPendingSubmit(const QString& pageUrl, const QString& formId);

private:
    explicit FormRecoveryController(QObject* parent = nullptr);
    static FormRecoveryController* s_instance;
    QSqlDatabase m_db;
};

// ═══════════════════════════════════════════ FormRecoveryController.cpp ═══════════════════════════════════════════
#include "FormRecoveryController.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QJSEngine>
#include <QCoreApplication>

FormRecoveryController* FormRecoveryController::s_instance = nullptr;

FormRecoveryController* FormRecoveryController::instance() {
    if (!s_instance) {
        s_instance = new FormRecoveryController(qApp);
    }
    return s_instance;
}

FormRecoveryController::FormRecoveryController(QObject* parent) : QObject(parent) {
    initialize();
}

FormRecoveryController::~FormRecoveryController() {
    if (m_db.isOpen()) m_db.close();
}

void FormRecoveryController::initialize() {
    QString dbPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dbPath);
    QString dbFile = dbPath + "/sabre_forms.db";

    m_db = QSqlDatabase::addDatabase("QSQLITE", "sabre_forms_conn");
    m_db.setDatabaseName(dbFile);

    if (!m_db.open()) {
        qCritical() << "Failed to open Form Recovery DB:" << m_db.lastError().text();
        return;
    }

    QSqlQuery query(m_db);
    query.exec("CREATE TABLE IF NOT EXISTS form_data ("
               "page_url TEXT, form_id TEXT, data TEXT, pending_submit INTEGER DEFAULT 0, timestamp INTEGER, "
               "PRIMARY KEY (page_url, form_id))");
}

void FormRecoveryController::saveForm(const QString& pageUrl, const QString& formId, const QString& jsonData) {
    QSqlQuery query(m_db);
    query.prepare("INSERT OR REPLACE INTO form_data (page_url, form_id, data, timestamp) VALUES (:url, :id, :data, :time)");
    query.bindValue(":url", pageUrl);
    query.bindValue(":id", formId);
    query.bindValue(":data", jsonData);
    query.bindValue(":time", QDateTime::currentMSecsSinceEpoch());
    if (!query.exec()) qWarning() << "Failed to save form:" << query.lastError().text();
}

void FormRecoveryController::getFormsForPage(const QString& pageUrl, const QJSValue& callback) {
    QSqlQuery query(m_db);
    query.prepare("SELECT form_id, data FROM form_data WHERE page_url = :url");
    query.bindValue(":url", pageUrl);
    query.exec();

    QJsonObject root;
    while (query.next()) {
        QString id = query.value(0).toString();
        QString data = query.value(1).toString();
        root[id] = QJsonDocument::fromJson(data.toUtf8()).object();
    }

    if (callback.isCallable()) {
        QJSValueList args;
        args << QString(QJsonDocument(root).toJson(QJsonDocument::Compact));
        callback.call(args);
    }
}

void FormRecoveryController::markPendingSubmit(const QString& pageUrl, const QString& formId) {
    QSqlQuery query(m_db);
    query.prepare("UPDATE form_data SET pending_submit = 1 WHERE page_url = :url AND form_id = :id");
    query.bindValue(":url", pageUrl);
    query.bindValue(":id", formId);
    query.exec();
}

void FormRecoveryController::hasPendingSubmit(const QString& pageUrl, const QString& formId, const QJSValue& callback) {
    QSqlQuery query(m_db);
    query.prepare("SELECT pending_submit FROM form_data WHERE page_url = :url AND form_id = :id");
    query.bindValue(":url", pageUrl);
    query.bindValue(":id", formId);

    bool isPending = false;
    if (query.exec() && query.next()) isPending = query.value(0).toInt() == 1;

    if (callback.isCallable()) {
        QJSValueList args;
        args << isPending;
        callback.call(args);
    }
}

void FormRecoveryController::clearPendingSubmit(const QString& pageUrl, const QString& formId) {
    QSqlQuery query(m_db);
    query.prepare("UPDATE form_data SET pending_submit = 0 WHERE page_url = :url AND form_id = :id");
    query.bindValue(":url", pageUrl);
    query.bindValue(":id", formId);
    query.exec();
}

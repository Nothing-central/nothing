#include "LangManager.h"
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>

#ifndef NOTHING_LANG_DIR
#define NOTHING_LANG_DIR ""
#endif

LangManager::LangManager(QObject* parent) : QObject(parent) {
    QFile cfg(configPath());
    QString code = "en";
    if (cfg.open(QIODevice::ReadOnly)) {
        auto doc = QJsonDocument::fromJson(cfg.readAll());
        QString stored = doc.object().value("lang").toString();
        if (!stored.isEmpty()) code = stored;
        cfg.close();
    }
    load(code);
}

void LangManager::load(const QString& code) {
    QFile f(langFilePath(code));
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "Lang file not found:" << code;
        if (code != "en") { load("en"); return; }
        return;
    }
    auto doc = QJsonDocument::fromJson(f.readAll());
    m_strings = doc.object();
    m_lang = code;
    emit langChanged();
}

void LangManager::setLang(const QString& code) {
    if (code == m_lang) return;
    load(code);

    QFile cfg(configPath());
    QJsonObject obj;
    if (cfg.open(QIODevice::ReadOnly)) {
        obj = QJsonDocument::fromJson(cfg.readAll()).object();
        cfg.close();
    }
    obj["lang"] = code;
    if (cfg.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        cfg.write(QJsonDocument(obj).toJson());
        cfg.close();
    }
}

QString LangManager::get(const QString& key) const {
    QStringList parts = key.split('.');
    QJsonObject cur = m_strings;
    for (int i = 0; i < parts.size() - 1; ++i)
        cur = cur.value(parts[i]).toObject();
    return cur.value(parts.last()).toString(key);
}

bool LangManager::isFirstLaunch() const {
    QFile cfg(configPath());
    if (!cfg.open(QIODevice::ReadOnly)) return true;
    auto doc = QJsonDocument::fromJson(cfg.readAll());
    return !doc.object().value("launched").toBool(false);
}

void LangManager::markLaunched() {
    QFile cfg(configPath());
    QJsonObject obj;
    if (cfg.open(QIODevice::ReadOnly)) {
        obj = QJsonDocument::fromJson(cfg.readAll()).object();
        cfg.close();
    }
    obj["launched"] = true;
    obj["lang"] = m_lang;
    if (cfg.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        cfg.write(QJsonDocument(obj).toJson());
        cfg.close();
    }
}

QString LangManager::configPath() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir + "/sabre.json";
}

QString LangManager::langFilePath(const QString& code) const {
    return QString(NOTHING_LANG_DIR) + "/" + code + ".json";
}
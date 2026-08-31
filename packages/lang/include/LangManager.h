#pragma once
#include <QObject>
#include <QJsonObject>
#include <QString>

class LangManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString lang READ lang NOTIFY langChanged)

public:
    explicit LangManager(QObject* parent = nullptr);

    Q_INVOKABLE QString get(const QString& key) const;  // "toolbar.back" → "Back"
    Q_INVOKABLE void setLang(const QString& code);
    Q_INVOKABLE QString lang() const { return m_lang; }
    Q_INVOKABLE bool isFirstLaunch() const;
    Q_INVOKABLE void markLaunched();

signals:
    void langChanged();

private:
    void load(const QString& code);
    QString configPath() const;
    QString langFilePath(const QString& code) const;

    QString m_lang;
    QJsonObject m_strings;
};
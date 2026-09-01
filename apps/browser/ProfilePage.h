#pragma once
#include <QWidget>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include "FingerprintController.h"

class ProfilePage : public QWidget {
    Q_OBJECT
public:
    explicit ProfilePage(FingerprintController* fp, QWidget* parent = nullptr);

public slots:
    void refresh();

private:
    void buildUI();
    QWidget* makeSection(const QString& label, const QJsonObject& obj);

    FingerprintController* m_fp       = nullptr;
    QWidget*               m_content  = nullptr;
    QVBoxLayout*           m_contentLayout = nullptr;
    QLabel*                m_rawJson  = nullptr;
    QString                m_contextId = "default";
};
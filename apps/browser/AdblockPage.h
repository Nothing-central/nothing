#pragma once
#include <QWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QLabel>
#include <QPushButton>

class AdblockPage : public QWidget {
    Q_OBJECT
public:
    explicit AdblockPage(QWidget* parent = nullptr);

private slots:
    void onUpdateClicked();
    void onDownloadFinished(QNetworkReply* reply);

private:
    QNetworkAccessManager* m_networkManager;
    QLabel* m_statusLabel;
    int m_pendingDownloads;
    QString m_targetDir;
};

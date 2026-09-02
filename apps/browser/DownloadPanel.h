#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QMap>
#include "DownloadManager.h"

class DownloadItemWidget : public QWidget {
    Q_OBJECT
public:
    DownloadItemWidget(const DownloadRecord& rec, QWidget* parent = nullptr);
    void updateProgress(qint64 received, qint64 total);
    void setFinished(bool success);
private:
    QLabel* m_nameLabel;
    QLabel* m_statusLabel;
    QWidget* m_progressBar;
    QWidget* m_progressChunk;
    QPushButton* m_actionBtn;
};

class DownloadPanel : public QWidget {
    Q_OBJECT
public:
    explicit DownloadPanel(QWidget* parent = nullptr);
    void showRelativeTo(QWidget* widget); // Shows popup right below the button

private slots:
    void onDownloadStarted(const DownloadRecord& rec);
    void onDownloadProgress(const QString& id, qint64 received, qint64 total, int speed);
    void onDownloadFinished(const QString& id, bool success);
    void loadHistory();
    void openFullPage();

private:
    QVBoxLayout* m_layout;
    QScrollArea* m_scrollArea;
    QWidget* m_listContainer;
    QMap<QString, DownloadItemWidget*> m_items;
};

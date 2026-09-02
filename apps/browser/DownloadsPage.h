#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include "DownloadManager.h"

class DownloadsPage : public QWidget {
    Q_OBJECT
public:
    explicit DownloadsPage(QWidget* parent = nullptr);
};

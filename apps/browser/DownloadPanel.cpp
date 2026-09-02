#include "DownloadPanel.h"
#include <QHBoxLayout>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>

DownloadItemWidget::DownloadItemWidget(const DownloadRecord& rec, QWidget* parent)
    : QWidget(parent) {
    setFixedHeight(60);
    setStyleSheet("background:#161616; border-bottom:1px solid #262626; border-radius:4px; margin:4px;");

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 8);

    auto* iconLabel = new QLabel("⬇", this);
    iconLabel->setStyleSheet("color:#ff2d2d; font-size:18px;");

    auto* infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(2);

    m_nameLabel = new QLabel(rec.filename, this);
    m_nameLabel->setStyleSheet("color:#ffffff; font-size:12px; font-weight:bold;");
    m_nameLabel->setMaximumWidth(250);

    m_statusLabel = new QLabel("Starting...", this);
    m_statusLabel->setStyleSheet("color:#888888; font-size:10px;");

    m_progressBar = new QWidget(this);
    m_progressBar->setFixedHeight(4);
    m_progressBar->setStyleSheet("background:#262626; border-radius:2px;");

    m_progressChunk = new QWidget(m_progressBar);
    m_progressChunk->setStyleSheet("background:#ff2d2d; border-radius:2px;");
    m_progressChunk->setFixedWidth(0);

    infoLayout->addWidget(m_nameLabel);
    infoLayout->addWidget(m_statusLabel);
    infoLayout->addWidget(m_progressBar);

    m_actionBtn = new QPushButton("✕", this);
    m_actionBtn->setFixedSize(24, 24);
    m_actionBtn->setStyleSheet("QPushButton{background:transparent; color:#888; border:none;} QPushButton:hover{color:#ff2d2d;}");

    connect(m_actionBtn, &QPushButton::clicked, this, [this, rec]() {
        QString fullPath = rec.path + "/" + rec.filename;
        if (QFileInfo::exists(fullPath)) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(fullPath));
        }
    });

    layout->addWidget(iconLabel);
    layout->addLayout(infoLayout, 1);
    layout->addWidget(m_actionBtn);
}

void DownloadItemWidget::updateProgress(qint64 received, qint64 total) {
    if (total > 0) {
        double pct = (double)received / total;
        m_progressChunk->setFixedWidth(m_progressBar->width() * pct);
        m_statusLabel->setText(QString("%1% of %2 MB").arg((int)(pct*100)).arg(total / (1024*1024)));
    }
}

void DownloadItemWidget::setFinished(bool success) {
    m_progressChunk->setFixedWidth(m_progressBar->width());
    m_progressChunk->setStyleSheet(success ? "background:#2a8a2a; border-radius:2px;" : "background:#ff2d2d; border-radius:2px;");
    m_statusLabel->setText(success ? "Completed" : "Failed");
    m_actionBtn->setText("📂");
}

// ── DownloadPanel ────────────────────────────────────────────────────────────
DownloadPanel::DownloadPanel(QWidget* parent) : QWidget(parent) {
    // CRITICAL FIX: Qt::Popup ensures it floats OVER the UI and never shrinks the layout
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setFixedSize(360, 450);
    setStyleSheet("background:#111111; border:1px solid #262626; border-radius:6px;");

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    auto* header = new QWidget(this);
    header->setFixedHeight(46);
    header->setStyleSheet("background:#0e0e0e; border-bottom:1px solid #1c1c1c; border-top-left-radius:6px; border-top-right-radius:6px;");
    auto* hLayout = new QHBoxLayout(header);

    auto* title = new QLabel("🦴 Downloads", header);
    title->setStyleSheet("color:#ffffff; font-size:14px; font-weight:bold;");

    auto* fullPageBtn = new QPushButton("Full Page", header);
    fullPageBtn->setStyleSheet("QPushButton{background:transparent; color:#ff2d2d; border:none; font-size:11px;} QPushButton:hover{color:#ff6b6b;}");
    connect(fullPageBtn, &QPushButton::clicked, this, &DownloadPanel::openFullPage);

    auto* clearBtn = new QPushButton("Clear", header);
    clearBtn->setStyleSheet("QPushButton{background:transparent; color:#888; border:none; font-size:11px;} QPushButton:hover{color:#ff2d2d;}");
    connect(clearBtn, &QPushButton::clicked, this, []() {
        DownloadManager::instance()->clearHistory();
    });

    hLayout->addWidget(title);
    hLayout->addStretch();
    hLayout->addWidget(fullPageBtn);
    hLayout->addWidget(clearBtn);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setStyleSheet("QScrollArea { border: none; background: #111111; border-bottom-left-radius:6px; border-bottom-right-radius:6px; }");

    m_listContainer = new QWidget();
    m_listContainer->setStyleSheet("background:#111111; border-bottom-left-radius:6px; border-bottom-right-radius:6px;");
    m_scrollArea->setWidget(m_listContainer);

    m_layout->addWidget(header);
    m_layout->addWidget(m_scrollArea, 1);

    auto* mgr = DownloadManager::instance();
    connect(mgr, &DownloadManager::downloadStarted, this, &DownloadPanel::onDownloadStarted);
    connect(mgr, &DownloadManager::downloadProgress, this, &DownloadPanel::onDownloadProgress);
    connect(mgr, &DownloadManager::downloadFinished, this, &DownloadPanel::onDownloadFinished);
    connect(mgr, &DownloadManager::historyUpdated, this, &DownloadPanel::loadHistory);

    loadHistory();
}

void DownloadPanel::showRelativeTo(QWidget* widget) {
    if (!widget) return;
    // Position right below the button
    QPoint pos = widget->mapToGlobal(QPoint(0, widget->height() + 8));
    move(pos);
    show();
    raise();
    activateWindow();
}

void DownloadPanel::openFullPage() {
    hide(); // Close popup
    // Emit a signal or directly navigate the parent tab to about://downloads
    // We'll handle this via a custom signal or by finding the parent BrowserTab
    if (auto* tab = qobject_cast<QWidget*>(parent())) {
        // Assuming parent has a navigateTo slot
        QMetaObject::invokeMethod(tab, "navigateTo", Q_ARG(QString, "about://downloads"));
    }
}

void DownloadPanel::onDownloadStarted(const DownloadRecord& rec) {
    auto* item = new DownloadItemWidget(rec, m_listContainer);
    m_items[rec.id] = item;

    auto* layout = qobject_cast<QVBoxLayout*>(m_listContainer->layout());
    if (!layout) {
        layout = new QVBoxLayout(m_listContainer);
        layout->setContentsMargins(0,0,0,0);
        layout->setSpacing(0);
        layout->addStretch();
    }
    layout->insertWidget(0, item);
    showRelativeTo(qobject_cast<QWidget*>(parent())); // Auto-show
}

void DownloadPanel::onDownloadProgress(const QString& id, qint64 received, qint64 total, int speed) {
    if (m_items.contains(id)) m_items[id]->updateProgress(received, total);
}

void DownloadPanel::onDownloadFinished(const QString& id, bool success) {
    if (m_items.contains(id)) m_items[id]->setFinished(success);
}

void DownloadPanel::loadHistory() {
    for (auto* item : m_items) item->deleteLater();
    m_items.clear();

    auto records = DownloadManager::instance()->getHistory();
    auto* layout = qobject_cast<QVBoxLayout*>(m_listContainer->layout());
    if (!layout) {
        layout = new QVBoxLayout(m_listContainer);
        layout->setContentsMargins(0,0,0,0);
        layout->setSpacing(0);
        layout->addStretch();
    }

    for (const auto& rec : records) {
        auto* item = new DownloadItemWidget(rec, m_listContainer);
        if (rec.status == "completed") item->setFinished(true);
        else if (rec.status == "failed") item->setFinished(false);
        m_items[rec.id] = item;
        layout->insertWidget(layout->count() - 1, item);
    }
}

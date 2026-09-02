#include "AdblockPage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QMessageBox>

AdblockPage::AdblockPage(QWidget* parent) : QWidget(parent), m_pendingDownloads(0) {
    m_targetDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/filters";
    QDir().mkpath(m_targetDir);

    m_networkManager = new QNetworkAccessManager(this);
    connect(m_networkManager, &QNetworkAccessManager::finished, this, &AdblockPage::onDownloadFinished);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(40, 40, 40, 40);
    root->setSpacing(20);
    root->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

    // ── Praise nothing-adblock ──────────────────────────────────────────────
    auto* title = new QLabel("🛡️ nothing-adblock", this);
    title->setStyleSheet("color: #ffffff; font-size: 32px; font-weight: 600; font-family: 'GeistMono', monospace;");

    auto* subtitle = new QLabel(
        "A from-scratch C++ adblock engine built for the Nothing ecosystem.<br>"
        "No dependencies. No Rust. No bloat.<br><br>"
        "<span style='color: #888888; font-size: 12px;'>"
        "Powered by the 5-core engine: Tokenizer, TokenSelector, FlatMultiMap, NetworkFilter, and Blocker.<br>"
        "Evaluates incoming URLs in microseconds with zero rendering overhead."
        "</span>", this);
    subtitle->setStyleSheet("color: #cccccc; font-size: 14px; font-family: 'Geist-Light', sans-serif; line-height: 1.6;");
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setWordWrap(true);

    // ── Update Button ───────────────────────────────────────────────────────
    auto* updateBtn = new QPushButton("Update Filter Lists", this);
    updateBtn->setFixedSize(220, 44);
    updateBtn->setCursor(Qt::PointingHandCursor);
    updateBtn->setStyleSheet(R"(
        QPushButton {
            background: #ff2d2d; color: #ffffff; border: none; border-radius: 4px;
            font-family: 'GeistMono', monospace; font-size: 13px; font-weight: 600;
        }
        QPushButton:hover { background: #e02525; }
        QPushButton:pressed { background: #cc0000; }
        QPushButton:disabled { background: #555555; color: #888888; }
    )");

    m_statusLabel = new QLabel("Ready. Updates apply on next browser restart.", this);
    m_statusLabel->setStyleSheet("color: #888888; font-size: 11px; font-family: 'GeistMono', monospace;");
    m_statusLabel->setAlignment(Qt::AlignCenter);

    connect(updateBtn, &QPushButton::clicked, this, &AdblockPage::onUpdateClicked);

    root->addSpacing(40);
    root->addWidget(title);
    root->addWidget(subtitle);
    root->addSpacing(30);
    root->addWidget(updateBtn);
    root->addSpacing(10);
    root->addWidget(m_statusLabel);
    root->addStretch(1);
}

void AdblockPage::onUpdateClicked() {
    m_pendingDownloads = 2;
    m_statusLabel->setText("Downloading latest lists from Avast/EasyList mirror...");
    m_statusLabel->setStyleSheet("color: #ffaa00; font-size: 11px; font-family: 'GeistMono', monospace;");

    // Raw URLs derived from the provided GitHub paths
    QNetworkRequest reqList(QUrl("https://raw.githubusercontent.com/avast/adblock/master/src/assets/thirdparties/easylist-downloads.adblockplus.org/easylist.txt"));
    reqList.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_networkManager->get(reqList);

    QNetworkRequest reqPrivacy(QUrl("https://raw.githubusercontent.com/avast/adblock/master/src/assets/thirdparties/easylist-downloads.adblockplus.org/easyprivacy.txt"));
    reqPrivacy.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_networkManager->get(reqPrivacy);
}

void AdblockPage::onDownloadFinished(QNetworkReply* reply) {
    if (reply->error() == QNetworkReply::NoError) {
        QString fileName = reply->url().toString().contains("easyprivacy") ? "easyprivacy.txt" : "easylist.txt";
        QFile file(m_targetDir + "/" + fileName);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(reply->readAll());
            file.close();
        }
    } else {
        m_statusLabel->setText("Error: " + reply->errorString());
        m_statusLabel->setStyleSheet("color: #ff2d2d; font-size: 11px; font-family: 'GeistMono', monospace;");
    }

    reply->deleteLater();
    m_pendingDownloads--;

    if (m_pendingDownloads == 0) {
        m_statusLabel->setText("✅ Updated successfully! Restart Sabre to apply new rules.");
        m_statusLabel->setStyleSheet("color: #2a8a2a; font-size: 11px; font-family: 'GeistMono', monospace;");
    }
}

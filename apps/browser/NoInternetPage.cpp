// ═══════════════════════════════════════════ NoInternetPage.cpp ══════════════════════════════════════
#include "NoInternetPage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPixmap>
#include <QFontDatabase>

NoInternetPage::NoInternetPage(QWidget* parent)
    : QWidget(parent)
{
    setStyleSheet("background: #0e0e0e;");
    buildUI();
}

void NoInternetPage::buildUI() {
    // ── Root layout ───────────────────────────────────────────────────────────
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->setAlignment(Qt::AlignCenter);

    // ── Centre card ───────────────────────────────────────────────────────────
    auto* card = new QWidget(this);
    card->setFixedWidth(480);
    card->setStyleSheet("background: transparent;");

    auto* col = new QVBoxLayout(card);
    col->setContentsMargins(32, 32, 32, 32);
    col->setSpacing(20);
    col->setAlignment(Qt::AlignHCenter);

    // ── Illustration ──────────────────────────────────────────────────────────
    m_image = new QLabel(card);
    QPixmap pix(QString(SOURCE_DIR) + "/assets/icons/nointernetimagewithoutbackground.png");
    if (!pix.isNull()) {
        m_image->setPixmap(
            pix.scaled(220, 220, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    m_image->setAlignment(Qt::AlignCenter);
    m_image->setStyleSheet("background: transparent;");
    col->addWidget(m_image, 0, Qt::AlignHCenter);

    // ── Title ─────────────────────────────────────────────────────────────────
    m_title = new QLabel("No Internet Connection", card);
    m_title->setAlignment(Qt::AlignCenter);
    m_title->setStyleSheet(R"(
        color: #ffffff;
        font-family: "GeistMono", monospace;
        font-size: 22px;
        font-weight: 600;
        background: transparent;
    )");
    col->addWidget(m_title, 0, Qt::AlignHCenter);

    // ── Subtitle ──────────────────────────────────────────────────────────────
    m_subtitle = new QLabel(
        "Sabre can't reach the internet right now.\n"
        "Check your connection and try again.", card);
    m_subtitle->setAlignment(Qt::AlignCenter);
    m_subtitle->setWordWrap(true);
    m_subtitle->setStyleSheet(R"(
        color: #666666;
        font-family: "Geist", sans-serif;
        font-size: 13px;
        background: transparent;
        line-height: 1.6;
    )");
    col->addWidget(m_subtitle, 0, Qt::AlignHCenter);

    // ── Button row ────────────────────────────────────────────────────────────
    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(12);
    btnRow->setAlignment(Qt::AlignHCenter);

    // Retry button
    m_retryBtn = new QPushButton("Try Again", card);
    m_retryBtn->setFixedSize(130, 38);
    m_retryBtn->setCursor(Qt::PointingHandCursor);
    m_retryBtn->setStyleSheet(R"(
        QPushButton {
            background: #1c1c1c;
            color: #cccccc;
            border: 1px solid #2a2a2a;
            border-radius: 4px;
            font-family: "GeistMono", monospace;
            font-size: 12px;
        }
        QPushButton:hover   { background: #242424; color: #ffffff; border-color: #3a3a3a; }
        QPushButton:pressed { background: #111111; }
    )");
    connect(m_retryBtn, &QPushButton::clicked, this, &NoInternetPage::retryRequested);

    // Offline game button
    m_gameBtn = new QPushButton("Play Offline Game", card);
    m_gameBtn->setFixedSize(160, 38);
    m_gameBtn->setCursor(Qt::PointingHandCursor);
    m_gameBtn->setStyleSheet(R"(
        QPushButton {
            background: #1a1a2e;
            color: #7b7bff;
            border: 1px solid #2a2a4a;
            border-radius: 4px;
            font-family: "GeistMono", monospace;
            font-size: 12px;
        }
        QPushButton:hover   { background: #22223a; color: #9999ff; border-color: #3a3a6a; }
        QPushButton:pressed { background: #111120; }
    )");
    connect(m_gameBtn, &QPushButton::clicked, this, &NoInternetPage::playOfflineGame);

    btnRow->addWidget(m_retryBtn);
    btnRow->addWidget(m_gameBtn);
    col->addLayout(btnRow);

    root->addWidget(card, 0, Qt::AlignCenter);
}

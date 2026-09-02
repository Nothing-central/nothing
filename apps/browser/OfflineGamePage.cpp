// ═══════════════════════════════════════════ OfflineGamePage.cpp ═════════════════════════════════════
#include "OfflineGamePage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

OfflineGamePage::OfflineGamePage(QWidget* parent)
    : QWidget(parent)
{
    setStyleSheet("background: #0a0a12;");
    buildUI();
}

void OfflineGamePage::buildUI() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setAlignment(Qt::AlignCenter);

    auto* card = new QWidget(this);
    card->setFixedWidth(520);
    card->setStyleSheet("background: transparent;");

    auto* col = new QVBoxLayout(card);
    col->setContentsMargins(40, 40, 40, 40);
    col->setSpacing(18);
    col->setAlignment(Qt::AlignHCenter);

    // ── Big pixel-art-ish "GAME" label ────────────────────────────────────────
    auto* gameLogo = new QLabel("[ SABRE OFFLINE ]", card);
    gameLogo->setAlignment(Qt::AlignCenter);
    gameLogo->setStyleSheet(R"(
        color: #7b7bff;
        font-family: "GeistPixel", monospace;
        font-size: 28px;
        letter-spacing: 4px;
        background: transparent;
    )");
    col->addWidget(gameLogo, 0, Qt::AlignHCenter);

    // ── Separator line ────────────────────────────────────────────────────────
    auto* sep = new QFrame(card);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color: #1e1e3a;");
    col->addWidget(sep);

    // ── Under-development message ─────────────────────────────────────────────
    auto* msg = new QLabel(
        "The game is currently under development.\n\n"
        "Please wait for a major release — something good is coming.\n"
        "Stay offline. Stay patient. Stay legendary.", card);
    msg->setAlignment(Qt::AlignCenter);
    msg->setWordWrap(true);
    msg->setStyleSheet(R"(
        color: #888888;
        font-family: "GeistMono", monospace;
        font-size: 13px;
        line-height: 1.7;
        background: transparent;
    )");
    col->addWidget(msg, 0, Qt::AlignHCenter);

    // ── Version badge ─────────────────────────────────────────────────────────
    auto* badge = new QLabel("v0.0.0 — coming soon™", card);
    badge->setAlignment(Qt::AlignCenter);
    badge->setStyleSheet(R"(
        color: #2a2a4a;
        font-family: "GeistMono", monospace;
        font-size: 11px;
        background: transparent;
    )");
    col->addWidget(badge, 0, Qt::AlignHCenter);

    // ── Back button ───────────────────────────────────────────────────────────
    auto* backBtn = new QPushButton("← Back", card);
    backBtn->setFixedSize(110, 36);
    backBtn->setCursor(Qt::PointingHandCursor);
    backBtn->setStyleSheet(R"(
        QPushButton {
            background: #111111;
            color: #666666;
            border: 1px solid #1e1e1e;
            border-radius: 4px;
            font-family: "GeistMono", monospace;
            font-size: 12px;
        }
        QPushButton:hover   { background: #1a1a1a; color: #aaaaaa; border-color: #2a2a2a; }
        QPushButton:pressed { background: #0a0a0a; }
    )");
    connect(backBtn, &QPushButton::clicked, this, &OfflineGamePage::backRequested);
    col->addWidget(backBtn, 0, Qt::AlignHCenter);

    root->addWidget(card, 0, Qt::AlignCenter);
}

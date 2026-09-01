#include "ProfilePage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QPushButton>
#include <QFrame>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QApplication>
#include <QClipboard>
#include <QMessageBox>

static const QString kMono   = R"(font-family:"GeistMono",monospace;)";
static const QString kBg     = "background:#080808;";

ProfilePage::ProfilePage(FingerprintController* fp, QWidget* parent)
    : QWidget(parent), m_fp(fp)
{
    setStyleSheet("QWidget { " + kBg + " }");
    buildUI();
}

void ProfilePage::buildUI() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Refresh button (top-right, outside scroll) ────────────────────────────
    auto* topBar = new QHBoxLayout;
    topBar->setContentsMargins(32, 20, 32, 0);
    topBar->addStretch(1);

    auto* refreshBtn = new QPushButton("REFRESH", this);
    refreshBtn->setFixedSize(80, 28);
    refreshBtn->setCursor(Qt::PointingHandCursor);
    refreshBtn->setStyleSheet(R"(
        QPushButton {
            background:#111111; color:#333333;
            border:1px solid #1f1f1f; border-radius:2px;
            font-family:"GeistMono",monospace; font-size:9px; letter-spacing:2px;
        }
        QPushButton:hover { background:#161616; }
    )");
    connect(refreshBtn, &QPushButton::clicked, this, &ProfilePage::refresh);
    topBar->addWidget(refreshBtn);
    root->addLayout(topBar);

    // ── Scrollable content ────────────────────────────────────────────────────
    auto* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setStyleSheet("QScrollArea { background:#080808; border:none; }");

    m_content = new QWidget;
    m_content->setStyleSheet("background:#080808;");
    m_contentLayout = new QVBoxLayout(m_content);
    m_contentLayout->setContentsMargins(40, 32, 40, 40);  // Updated margins
    m_contentLayout->setSpacing(32);  // Updated spacing

    scroll->setWidget(m_content);
    scroll->setWidgetResizable(true);

    root->addWidget(scroll, 1);

    refresh();
}

void ProfilePage::refresh() {
    // Clear existing content
    while (QLayoutItem* item = m_contentLayout->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    QString raw = m_fp->identityJson(m_contextId);
    QJsonObject data;
    auto doc = QJsonDocument::fromJson(raw.toUtf8());
    if (doc.isObject()) data = doc.object();

    auto makeLbl = [&](const QString& text, const QString& style) -> QLabel* {
        auto* l = new QLabel(text, m_content);
        l->setStyleSheet(style);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return l;
    };

    // ── Header ────────────────────────────────────────────────────────────────
    auto* headerCol = new QVBoxLayout;
    headerCol->setSpacing(6);

    auto* tagRow = new QHBoxLayout;
    auto* dot = new QLabel("●", m_content);
    dot->setStyleSheet("color:#ff2d2d; font-size:8px; background:transparent;");
    auto* tag = makeLbl("ACTIVE IDENTITY",
        "color:#ff2d2d; " + kMono + " font-size:13px; letter-spacing:4px; background:transparent;");  // Updated size
    tagRow->addWidget(dot);
    tagRow->addWidget(tag);
    tagRow->addStretch();

    headerCol->addLayout(tagRow);
    headerCol->addWidget(makeLbl("about://profile",
        "color:#ffffff; " + kMono + " font-size:26px; background:transparent;"));  // Updated size
    headerCol->addWidget(makeLbl("context: " + (data.value("contextId").toString().isEmpty()
                                                 ? m_contextId : data.value("contextId").toString()),
        "color:#333333; " + kMono + " font-size:12px; letter-spacing:2px; background:transparent;"));  // Updated size

    auto* headerW = new QWidget(m_content);
    headerW->setLayout(headerCol);
    m_contentLayout->addWidget(headerW);

    // ── Divider ───────────────────────────────────────────────────────────────
    auto* div = new QFrame(m_content);
    div->setFrameShape(QFrame::HLine);
    div->setStyleSheet("background:#111111; border:none; max-height:1px;");
    m_contentLayout->addWidget(div);

    // ── Sections ──────────────────────────────────────────────────────────────
    for (auto& key : {"navigator", "screen", "webgl", "audio"}) {
        QJsonObject obj;
        if (data.value(key).isObject()) obj = data.value(key).toObject();
        m_contentLayout->addWidget(makeSection(QString(key).toUpper(), obj));
    }

    // ── Raw JSON ──────────────────────────────────────────────────────────────
    auto* rawLabel = makeLbl("RAW JSON",
        "color:#333333; " + kMono + " font-size:11px; letter-spacing:3px; background:transparent;");  // Updated size
    m_contentLayout->addWidget(rawLabel);

    auto* rawBox = new QWidget(m_content);
    rawBox->setStyleSheet("background:#0d0d0d; border:1px solid #161616; border-radius:2px;");
    auto* rawLayout = new QVBoxLayout(rawBox);
    rawLayout->setContentsMargins(12, 12, 12, 12);

    // ── Raw JSON with copy button ─────────────────────────────────────────────
    auto* rawContainer = new QWidget(m_content);
    auto* rawContainerLayout = new QHBoxLayout(rawContainer);
    rawContainerLayout->setContentsMargins(0, 0, 0, 0);
    rawContainerLayout->setSpacing(8);

    auto* rawText = makeLbl(raw,
        "color:#444444; " + kMono + " font-size:10px; background:transparent;");
    rawText->setWordWrap(true);
    rawContainerLayout->addWidget(rawText, 1);

    // Copy button
    auto* copyBtn = new QPushButton("COPY", rawBox);
    copyBtn->setFixedSize(60, 24);
    copyBtn->setCursor(Qt::PointingHandCursor);
    copyBtn->setStyleSheet(R"(
        QPushButton {
            background:#111111; color:#555555;
            border:1px solid #1f1f1f; border-radius:2px;
            font-family:"GeistMono",monospace; font-size:8px; letter-spacing:1px;
        }
        QPushButton:hover { background:#161616; color:#888888; }
    )");
    connect(copyBtn, &QPushButton::clicked, [this, raw]() {
        QClipboard* clipboard = QApplication::clipboard();
        clipboard->setText(raw);
        QMessageBox::information(this, "Copied", "JSON copied to clipboard!");
    });
    rawContainerLayout->addWidget(copyBtn);

    rawLayout->addWidget(rawContainer);
    m_contentLayout->addWidget(rawBox);

    m_contentLayout->addStretch();
}

QWidget* ProfilePage::makeSection(const QString& label, const QJsonObject& obj) {
    auto* w = new QWidget(m_content);
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(10);

    auto* lbl = new QLabel(label, w);
    lbl->setStyleSheet("color:#333333; font-family:\"GeistMono\",monospace; font-size:11px; letter-spacing:3px; background:transparent;");  // Updated size
    vl->addWidget(lbl);

    auto* box = new QWidget(w);
    box->setStyleSheet("background:#0d0d0d; border:1px solid #161616; border-radius:2px;");
    auto* bl = new QVBoxLayout(box);
    bl->setContentsMargins(8, 8, 8, 8);
    bl->setSpacing(0);

    QStringList keys = obj.keys();
    for (int i = 0; i < keys.size(); i++) {
        auto* row = new QWidget(box);
        row->setFixedHeight(36);  // Updated height
        row->setStyleSheet(i % 2 == 0
            ? "background:transparent;"
            : "background:#080808;");

        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(8, 0, 8, 0);
        rl->setSpacing(0);

        auto* keyLbl = new QLabel(keys[i], row);
        keyLbl->setFixedWidth(260);  // Updated width
        keyLbl->setStyleSheet("color:#444444; font-family:\"GeistMono\",monospace; font-size:13px; background:transparent;");  // Updated size
        keyLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);

        QJsonValue v = obj.value(keys[i]);
        QString valStr;
        if (v.isNull() || v.isUndefined()) valStr = "—";
        else if (v.isString()) valStr = v.toString();
        else if (v.isBool())   valStr = v.toBool() ? "true" : "false";
        else if (v.isDouble()) valStr = QString::number(v.toDouble());
        else valStr = QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact);

        auto* valLbl = new QLabel(valStr, row);
        valLbl->setStyleSheet("color:#888888; font-family:\"GeistMono\",monospace; font-size:13px; background:transparent;");  // Updated size
        valLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);

        rl->addWidget(keyLbl);
        rl->addWidget(valLbl, 1);
        bl->addWidget(row);
    }

    vl->addWidget(box);
    return w;
}
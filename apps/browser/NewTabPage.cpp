// ═══════════════════════════════════════════ NewTabPage.cpp ═════════════════════════════════════════════
#include "NewTabPage.h"
#include "SearchController.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDateTime>
#include <QTimeZone>
#include <QUrl>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QPixmap>
#include <QResizeEvent>
#include <QPainter>
#include <QPalette>
#include <QSettings>
#include <QLinearGradient>

NewTabPage::NewTabPage(SearchController* searchController, QWidget* parent)
    : QWidget(parent), m_searchController(searchController)
{
    QSettings s("Ernest Tech House", "Sabre Browser");
    m_bgMode       = s.value("newtab/bgMode", 0).toInt();
    m_bgCustomPath = s.value("newtab/bgCustomPath", "").toString();
    if (m_bgMode == 1 && !m_bgCustomPath.isEmpty())
        m_bgPixmap = QPixmap(m_bgCustomPath);
    else
        m_bgPixmap = QPixmap(":/sabre/images/elysiamain.jpeg");
    buildUI();
}

void NewTabPage::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    update();
}

void NewTabPage::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    switch (m_bgMode) {
        case 0:
        case 1: {
            if (!m_bgPixmap.isNull()) {
                QPixmap scaled = m_bgPixmap.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                int x = (scaled.width()  - width())  / 2;
                int y = (scaled.height() - height()) / 2;
                painter.drawPixmap(0, 0, scaled, x, y, width(), height());
            } else {
                painter.fillRect(rect(), QColor("#0a0a0a"));
            }
            break;
        }
        case 2:
            painter.fillRect(rect(), QColor("#0a0a0a"));
            break;
        case 3: {
            QLinearGradient grad(0, 0, 0, height());
            grad.setColorAt(0.0, QColor("#0d0d1a"));
            grad.setColorAt(0.5, QColor("#0a0a0a"));
            grad.setColorAt(1.0, QColor("#1a0a0d"));
            painter.fillRect(rect(), grad);
            break;
        }
    }
}

void NewTabPage::buildUI() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* topBar = new QWidget(this);
    topBar->setStyleSheet("background: rgba(0,0,0,160); border-bottom: 1px solid rgba(255,255,255,15);");
    topBar->setFixedHeight(80);

    auto* tbl = new QHBoxLayout(topBar);
    tbl->setContentsMargins(28, 0, 28, 0);
    tbl->setSpacing(0);

    auto* logoLabel = new QLabel(topBar);
    QPixmap logo(":/sabre/icons/mainlogo-nobackground.png");
    if (!logo.isNull())
        logoLabel->setPixmap(logo.scaled(52, 52, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logoLabel->setStyleSheet("background:transparent; border:none;");
    tbl->addWidget(logoLabel);
    tbl->addSpacing(20);

    auto* vdiv = new QFrame(topBar);
    vdiv->setFrameShape(QFrame::VLine);
    vdiv->setFixedHeight(40);
    vdiv->setStyleSheet("color: rgba(255,255,255,20);");
    tbl->addWidget(vdiv);
    tbl->addSpacing(20);

    auto* clockCol = new QVBoxLayout;
    clockCol->setSpacing(2);
    clockCol->setAlignment(Qt::AlignVCenter);

    m_clock = new QLabel("00:00:00", topBar);
    m_clock->setStyleSheet(R"(
        color: #ffffff;
        font-family: "GeistMono", monospace;
        font-size: 32px;
        font-weight: 300;
        letter-spacing: -1px;
        background: transparent;
    )");

    m_date = new QLabel("", topBar);
    m_date->setStyleSheet(R"(
        color: rgba(255,255,255,120);
        font-family: "GeistMono", monospace;
        font-size: 10px;
        letter-spacing: 3px;
        background: transparent;
    )");

    m_tz = new QLabel("", topBar);
    m_tz->setStyleSheet(R"(
        color: rgba(255,255,255,60);
        font-family: "GeistMono", monospace;
        font-size: 9px;
        letter-spacing: 2px;
        background: transparent;
    )");

    clockCol->addWidget(m_clock);
    clockCol->addWidget(m_date);
    clockCol->addWidget(m_tz);
    tbl->addLayout(clockCol);

    tbl->addStretch(1);

    auto* calRow = new QHBoxLayout;
    calRow->setSpacing(6);
    calRow->setAlignment(Qt::AlignVCenter);

    QDate today = QDate::currentDate();
    QDate weekStart = today.addDays(-(today.dayOfWeek() - 1));
    QStringList dayNames = {"M","T","W","T","F","S","S"};

    for (int i = 0; i < 7; i++) {
        QDate d = weekStart.addDays(i);
        bool isToday = (d == today);

        auto* tile = new QWidget(topBar);
        tile->setFixedSize(32, 48);
        tile->setStyleSheet(isToday
            ? "background: rgba(255,45,45,200); border-radius: 4px;"
            : "background: rgba(255,255,255,12); border-radius: 4px;");

        auto* tl = new QVBoxLayout(tile);
        tl->setContentsMargins(0, 4, 0, 4);
        tl->setSpacing(2);
        tl->setAlignment(Qt::AlignCenter);

        auto* dayName = new QLabel(dayNames[i], tile);
        dayName->setAlignment(Qt::AlignHCenter);
        dayName->setStyleSheet(isToday
            ? "color: rgba(255,255,255,200); font-family:'GeistMono',monospace; font-size:8px; background:transparent;"
            : "color: rgba(255,255,255,80);  font-family:'GeistMono',monospace; font-size:8px; background:transparent;");

        auto* dayNum = new QLabel(QString::number(d.day()), tile);
        dayNum->setAlignment(Qt::AlignHCenter);
        dayNum->setStyleSheet(isToday
            ? "color: #ffffff; font-family:'GeistMono',monospace; font-size:14px; font-weight:600; background:transparent;"
            : "color: rgba(255,255,255,160); font-family:'GeistMono',monospace; font-size:14px; background:transparent;");

        tl->addWidget(dayName);
        tl->addWidget(dayNum);
        calRow->addWidget(tile);
    }

    tbl->addLayout(calRow);
    tbl->addSpacing(20);

    auto makeNavBtn = [&](const QString& iconPath, const QString& tooltip) {
        auto* btn = new QPushButton(topBar);
        btn->setIcon(QIcon(":/sabre/icons/" + iconPath));
        btn->setIconSize(QSize(16, 16));
        btn->setFixedSize(32, 32);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(tooltip);
        btn->setStyleSheet(R"(
            QPushButton {
                background: rgba(255,255,255,10);
                border: 1px solid rgba(255,255,255,20);
                border-radius: 4px;
            }
            QPushButton:hover   { background: rgba(255,255,255,20); border-color: rgba(255,255,255,40); }
            QPushButton:pressed { background: rgba(0,0,0,40); }
        )");
        return btn;
    };

    auto* profileBtn  = makeNavBtn("mainlogo-nobackground.png", "Profile");
    auto* settingsBtn = makeNavBtn("settings.svg", "Settings");

    connect(profileBtn,  &QPushButton::clicked, this, &NewTabPage::showProfile);
    connect(settingsBtn, &QPushButton::clicked, this, &NewTabPage::showSettings);

    tbl->addWidget(profileBtn);
    tbl->addSpacing(6);
    tbl->addWidget(settingsBtn);

    root->addWidget(topBar);

    root->addStretch(1);

    auto* searchWrap = new QHBoxLayout;
    searchWrap->setContentsMargins(0, 0, 0, 0);
    searchWrap->addStretch(1);

    auto* searchBox = new QWidget(this);
    searchBox->setFixedWidth(620);
    searchBox->setFixedHeight(52);
    searchBox->setStyleSheet(R"(
        QWidget {
            background: rgba(10,10,10,180);
            border: 1px solid rgba(255,255,255,20);
            border-radius: 4px;
        }
    )");

    auto* sbl = new QHBoxLayout(searchBox);
    sbl->setContentsMargins(18, 0, 18, 0);
    sbl->setSpacing(12);

    auto* slash = new QLabel("/", searchBox);
    slash->setStyleSheet("color:#ff2d2d; font-size:18px; font-family:'GeistMono',monospace; border:none; background:transparent;");

    m_search = new QLineEdit(searchBox);
    m_search->setPlaceholderText("SEARCH_");
    m_search->setStyleSheet(R"(
        QLineEdit {
            background: transparent;
            color: #ffffff;
            border: none;
            font-family: "GeistMono", monospace;
            font-size: 14px;
        }
    )");

    sbl->addWidget(slash);
    sbl->addWidget(m_search, 1);
    searchWrap->addWidget(searchBox);
    searchWrap->addStretch(1);
    root->addLayout(searchWrap);

    root->addSpacing(10);

    m_engineLabel = new QLabel(this);
    m_engineLabel->setAlignment(Qt::AlignHCenter);
    m_engineLabel->setStyleSheet(R"(
        color: rgba(255,255,255,60);
        font-family: "GeistMono", monospace;
        font-size: 9px;
        letter-spacing: 3px;
        background: transparent;
    )");
    m_engineLabel->setContentsMargins(0, 4, 0, 0);
    root->addWidget(m_engineLabel);
    refreshEngineLabel();

    root->addStretch(1);

    auto* favLabel = new QLabel("FAVOURITES", this);
    favLabel->setAlignment(Qt::AlignHCenter);
    favLabel->setStyleSheet(R"(
        color: rgba(255,255,255,50);
        font-family: "GeistMono", monospace;
        font-size: 9px;
        letter-spacing: 4px;
        background: transparent;
    )");
    root->addWidget(favLabel);
    root->addSpacing(12);

    struct Fav { QString icon; QString label; QString url; };
    QList<Fav> favs = {
        {"🔍", "BRAVE",  "https://search.brave.com"},
        {"🐙", "GITHUB", "https://github.com"},
        {"📦", "NPM",    "https://npmjs.com"},
    };

    auto* favRow = new QHBoxLayout;
    favRow->setContentsMargins(0, 0, 0, 0);
    favRow->setSpacing(12);
    favRow->setAlignment(Qt::AlignHCenter);

    for (auto& f : favs) {
        auto* tile = new QWidget(this);
        tile->setFixedSize(100, 60);
        tile->setCursor(Qt::PointingHandCursor);
        tile->setStyleSheet("background: rgba(10,10,10,160); border: 1px solid rgba(255,255,255,15); border-radius: 4px;");

        auto* tl = new QVBoxLayout(tile);
        tl->setAlignment(Qt::AlignCenter);
        tl->setSpacing(6);

        auto* icon = new QLabel(f.icon, tile);
        icon->setAlignment(Qt::AlignHCenter);
        icon->setStyleSheet("font-size:18px; border:none; background:transparent;");

        auto* lbl = new QLabel(f.label, tile);
        lbl->setAlignment(Qt::AlignHCenter);
        lbl->setStyleSheet("color: rgba(255,255,255,120); font-family:'GeistMono',monospace; font-size:9px; letter-spacing:2px; border:none; background:transparent;");

        tl->addWidget(icon);
        tl->addWidget(lbl);

        QString url = f.url;
        auto* btn = new QPushButton(tile);
        btn->setGeometry(0, 0, 100, 60);
        btn->setFlat(true);
        btn->setStyleSheet("background:transparent; border:none;");
        connect(btn, &QPushButton::clicked, this, [this, url] {
            emit navigateRequested(url);
        });

        favRow->addWidget(tile);
    }

    root->addLayout(favRow);
    root->addStretch(2);

    auto* bottomBar = new QWidget(this);
    bottomBar->setFixedHeight(36);
    bottomBar->setStyleSheet("background: rgba(0,0,0,140); border-top: 1px solid rgba(255,255,255,10);");

    auto* bbl = new QHBoxLayout(bottomBar);
    bbl->setContentsMargins(20, 0, 20, 0);

    auto* attrLabel = new QLabel("art: @saltyAom", bottomBar);
    attrLabel->setStyleSheet(R"(
        color: rgba(255,255,255,30);
        font-family: "GeistMono", monospace;
        font-size: 8px;
        letter-spacing: 2px;
        background: transparent;
    )");

    bbl->addWidget(attrLabel);
    bbl->addStretch(1);

    auto* docsLabel = new QLabel(
        "for more information about nothing browser visit  "
        "<a href='https://nothing-browser-docs.pages.dev/' "
        "style='color:rgba(255,255,255,80); text-decoration:none; "
        "font-family:GeistMono,monospace; font-size:8px; letter-spacing:2px;'>"
        "nothing-browser-docs.pages.dev</a>",
        bottomBar);
    docsLabel->setTextFormat(Qt::RichText);
    docsLabel->setOpenExternalLinks(false);
    docsLabel->setStyleSheet(R"(
        color: rgba(255,255,255,30);
        font-family: "GeistMono", monospace;
        font-size: 8px;
        letter-spacing: 2px;
        background: transparent;
    )");
    connect(docsLabel, &QLabel::linkActivated, this, [this](const QString& url) {
        emit navigateRequested(url);
    });

    bbl->addWidget(docsLabel);
    root->addWidget(bottomBar);

    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &NewTabPage::onTick);
    m_timer->start();
    onTick();

    connect(m_search, &QLineEdit::returnPressed, this, &NewTabPage::onSearch);
}

void NewTabPage::refreshEngineLabel() {
    if (!m_engineLabel) return;
    QString engineName;
    if (m_searchController) {
        QString id = m_searchController->currentEngineId("default");
        engineName = m_searchController->engineDisplayName(id).toUpper();
    } else {
        engineName = "BRAVE SEARCH";
    }
    m_engineLabel->setText(engineName + "  ·  CHANGE IN SETTINGS");
}

void NewTabPage::onTick() {
    QDateTime now = QDateTime::currentDateTime();
    m_clock->setText(now.toString("HH:mm:ss"));
    m_date->setText(now.toString("ddd").toUpper() + "  ·  " +
                    now.toString("d MMM yyyy").toUpper());
    m_tz->setText(QTimeZone::systemTimeZone().abbreviation(now));
}

void NewTabPage::onSearch() {
    QString url = resolveInput(m_search->text().trimmed());
    if (!url.isEmpty()) {
        m_search->clear();
        emit navigateRequested(url);
    }
}

QString NewTabPage::resolveInput(const QString& raw) {
    if (raw.isEmpty()) return {};
    if (raw.startsWith("about://")) return raw;
    if (raw.contains('.') && !raw.contains(' '))
        return raw.startsWith("http") ? raw : "https://" + raw;
    if (m_searchController)
        return m_searchController->buildQueryUrl("default", raw);
    return "https://search.brave.com/search?q=" + QUrl::toPercentEncoding(raw);
}

void NewTabPage::setBackgroundMode(int mode, const QString& customPath) {
    m_bgMode       = mode;
    m_bgCustomPath = customPath;
    if (mode == 0)
        m_bgPixmap = QPixmap(":/sabre/images/elysiamain.jpeg");
    else if (mode == 1 && !customPath.isEmpty())
        m_bgPixmap = QPixmap(customPath);
    else
        m_bgPixmap = QPixmap();
    update();
}

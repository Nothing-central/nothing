// ═══════════════════════════════════════════ NormalWindow.cpp ═════════════════════════════════════════
#include "NormalWindow.h"
#include "SearchController.h"
#include <QHBoxLayout>
#include <QPushButton>
#include <QFontDatabase>
#include <QIcon>
#include <QSize>
#include <QSet>
#include <QFrame>

NormalWindow::NormalWindow(QWebEngineProfile* profile,
                           FingerprintController* fingerprint,
                           LangManager* lang,
                           SearchController* searchController,
                           QWidget* parent)
    : QMainWindow(parent), m_profile(profile), m_fingerprint(fingerprint),
      m_lang(lang), m_searchController(searchController)
{
    QFontDatabase::addApplicationFont(QString(SOURCE_DIR) + "/assets/fonts/GeistMono-Regular.ttf");
    QFontDatabase::addApplicationFont(QString(SOURCE_DIR) + "/assets/fonts/Geist-Light.ttf");
    QFontDatabase::addApplicationFont(QString(SOURCE_DIR) + "/assets/fonts/GeistPixel-Square.ttf");

    setWindowTitle("Sabre Browser");
    resize(1280, 800);
    setStyleSheet("QMainWindow { background:#0e0e0e; }");
    setupUI();
    addTab("about://home");
}

void NormalWindow::setupUI() {
    auto* central = new QWidget(this);
    auto* root    = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    setCentralWidget(central);

    // ── Tab bar ───────────────────────────────────────────────────────────────
    m_tabBar = new QWidget(central);
    m_tabBar->setFixedHeight(42);
    m_tabBar->setStyleSheet("background:#0c0c0c; border-bottom:1px solid #1a1a1a;");

    auto* tbl = new QHBoxLayout(m_tabBar);
    tbl->setContentsMargins(8, 5, 8, 0);
    tbl->setSpacing(4);

    // ── Tab bar icon button helper ────────────────────────────────────────────
    auto makeTabBtn = [&](const QString& iconPath, const QString& tooltip) {
        auto* btn = new QPushButton(m_tabBar);
        btn->setIcon(QIcon(QString(SOURCE_DIR) + "/assets/icons/" + iconPath));
        btn->setIconSize(QSize(15, 15));
        btn->setFixedSize(28, 28);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(tooltip);
        btn->setStyleSheet(R"(
            QPushButton {
                background: transparent;
                border: none;
                border-radius: 2px;
            }
            QPushButton:hover   { background: #1e1e1e; }
            QPushButton:pressed { background: #0a0a0a; }
        )");
        return btn;
    };

    auto* profileBtn  = makeTabBtn("mainlogo-nobackground.png", "Profile (about://profile)");
    auto* settingsBtn = makeTabBtn("settings.svg",              "Settings (about://settings)");

    connect(profileBtn,  &QPushButton::clicked, this, [this] {
        auto* tab = currentTab();
        if (tab) tab->navigateTo("about://profile");
    });
    connect(settingsBtn, &QPushButton::clicked, this, [this] {
        auto* tab = currentTab();
        if (tab) tab->navigateTo("about://settings");
    });

    tbl->addWidget(profileBtn);
    tbl->addWidget(settingsBtn);
    tbl->addSpacing(4);

    // ── Vertical separator ────────────────────────────────────────────────────
    auto* sep = new QFrame(m_tabBar);
    sep->setFrameShape(QFrame::VLine);
    sep->setFixedHeight(18);
    sep->setStyleSheet("color:#2a2a2a;");
    tbl->addWidget(sep);
    tbl->addSpacing(2);

    // ── Tab bar ───────────────────────────────────────────────────────────────
    m_tabs = new QTabBar(m_tabBar);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setExpanding(false);

    const QString closeIcon = QString("url(") + QString(SOURCE_DIR) + "/assets/icons/close.svg)";
    m_tabs->setStyleSheet(
        QStringLiteral(R"(
        QTabBar { background: transparent; }
        QTabBar::tab {
            background: transparent;
            color: #666666;
            border: 1px solid transparent;
            border-radius: 2px;
            padding: 4px 20px 4px 12px;
            min-width: 120px;
            max-width: 200px;
            font-family: "GeistMono", monospace;
            font-size: 11px;
        }
        QTabBar::tab:selected  { background:#1c1c1c; color:#ffffff; border-color:#2a2a2a; }
        QTabBar::tab:hover:!selected { background:#131313; color:#aaaaaa; }
        QTabBar::close-button {
            subcontrol-position: right;
            subcontrol-origin: padding;
            width: 14px;
            height: 14px;
            image: )") + closeIcon + QStringLiteral(R"(;
        }
        QTabBar::close-button:hover   { background:#2a2a2a; border-radius:2px; }
        QTabBar::close-button:pressed { background:#111111; border-radius:2px; }
    )"));

    // ── New tab button ────────────────────────────────────────────────────────
    auto* newTabBtn = new QPushButton("+", m_tabBar);
    newTabBtn->setFixedSize(28, 28);
    newTabBtn->setCursor(Qt::PointingHandCursor);
    newTabBtn->setStyleSheet(R"(
        QPushButton {
            background: transparent;
            color: #aaaaaa;
            border: 1px solid #2a2a2a;
            font-size: 18px;
            border-radius: 2px;
        }
        QPushButton:hover   { background: #1c1c1c; color: #ffffff; border-color:#3a3a3a; }
        QPushButton:pressed { background: #111111; }
    )");

    tbl->addWidget(m_tabs, 1);
    tbl->addWidget(newTabBtn);

    connect(m_tabs, &QTabBar::currentChanged,    this, &NormalWindow::switchTab);
    connect(m_tabs, &QTabBar::tabCloseRequested, this, &NormalWindow::closeTab);
    connect(newTabBtn, &QPushButton::clicked,    this, [this] { addTab("about://home"); });

    // ── Page stack ────────────────────────────────────────────────────────────
    m_pageStack = new QStackedWidget(central);

    m_stack = new QStackedWidget(m_pageStack);
    m_pageStack->addWidget(m_stack);                        // 0 = web tabs

    m_newTabPage = new NewTabPage(m_searchController, m_pageStack);

    connect(m_newTabPage, &NewTabPage::navigateRequested, this, [this](const QString& url) {
        static const QSet<QString> internal = {"about://home","about://profile","about://settings"};
        if (!internal.contains(url))
            m_pageStack->setCurrentIndex(0);
        auto* tab = currentTab();
        if (tab) tab->navigateTo(url);
    });

    connect(m_newTabPage, &NewTabPage::showProfile, this, [this] {
        auto* tab = currentTab();
        if (tab) tab->navigateTo("about://profile");
    });
    connect(m_newTabPage, &NewTabPage::showSettings, this, [this] {
        auto* tab = currentTab();
        if (tab) tab->navigateTo("about://settings");
    });

    m_pageStack->addWidget(m_newTabPage);                   // 1 = home

    m_profilePage = new ProfilePage(m_fingerprint, m_pageStack);
    m_pageStack->addWidget(m_profilePage);                  // 2 = profile

    m_settingsPage = new SettingsPage(m_fingerprint, m_profile, m_lang, m_searchController, m_pageStack);
    connect(m_settingsPage, &SettingsPage::profileChanged, this, [this](const QString&) {
        m_profilePage->refresh();
    });

    // ── CONNECT BACKGROUND MODE CHANGES FROM SETTINGS TO NEW TAB PAGE ──
    connect(m_settingsPage, &SettingsPage::backgroundModeChanged,
            m_newTabPage,   &NewTabPage::setBackgroundMode);
    // ─────────────────────────────────────────────────────────────────────

    if (m_searchController) {
        connect(m_searchController, &SearchController::engineChanged,
                m_newTabPage, &NewTabPage::refreshEngineLabel);
    }

    m_pageStack->addWidget(m_settingsPage);                 // 3 = settings

    root->addWidget(m_tabBar);
    root->addWidget(m_pageStack, 1);
}

void NormalWindow::addTab(const QString& url) {
    auto* tab = new BrowserTab(m_profile, m_searchController, m_fingerprint, this);
    connect(tab, &BrowserTab::urlChanged,   this, &NormalWindow::onTabUrlChanged);
    connect(tab, &BrowserTab::titleChanged, this, &NormalWindow::onTabTitleChanged);
    int idx = m_stack->addWidget(tab);
    m_tabs->addTab("New Tab");
    m_tabs->setCurrentIndex(idx);
    m_stack->setCurrentIndex(idx);
    tab->navigateTo(url);
}

void NormalWindow::closeTab(int index) {
    if (m_tabs->count() == 1) { close(); return; }
    auto* widget = m_stack->widget(index);
    m_stack->removeWidget(widget);
    widget->deleteLater();
    m_tabs->removeTab(index);
}

void NormalWindow::switchTab(int index) {
    if (index < 0 || index >= m_stack->count()) return;
    m_stack->setCurrentIndex(index);
    auto* tab = currentTab();
    if (tab) onTabUrlChanged(tab->currentUrl());
}

BrowserTab* NormalWindow::currentTab() const {
    return qobject_cast<BrowserTab*>(m_stack->currentWidget());
}

void NormalWindow::onTabUrlChanged(const QString& url) {
    static const QSet<QString> internal = {"about://home", "about://profile", "about://settings"};

    if (url.isEmpty() || url == "about:blank")
        return;

    if (internal.contains(url))
        showInternalPage(url);
    else
        m_pageStack->setCurrentIndex(0);

    int idx = m_tabs->currentIndex();
    if (idx >= 0 && internal.contains(url)) {
        QString label = url == "about://home"     ? "New Tab"
                      : url == "about://profile"  ? "Profile"
                                                  : "Settings";
        m_tabs->setTabText(idx, label);
    }
}

void NormalWindow::showInternalPage(const QString& url) {
    if (url == "about://home") {
        m_pageStack->setCurrentIndex(1);
    } else if (url == "about://profile") {
        m_profilePage->refresh();
        m_pageStack->setCurrentIndex(2);
    } else if (url == "about://settings") {
        m_pageStack->setCurrentIndex(3);
    }
}

void NormalWindow::onTabTitleChanged(const QString& title) {
    int idx = m_tabs->currentIndex();
    if (idx >= 0 && !title.isEmpty()) {
        QString label = title.length() > 28 ? title.left(28) + "…" : title;
        m_tabs->setTabText(idx, label);
    }
}

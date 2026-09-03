// ═══════════════════════════════════════════ IncognitoWindow.cpp ═════════════════════════════════════
#include "IncognitoWindow.h"
#include "FingerprintController.h"
#include "SearchController.h"
#include "BrowserTab.h"
#include "AdblockInterceptor.h"
#include "AdblockPage.h"
#include "SabreTabBar.h"
#include <QtWebEngineCore/QWebEngineCookieStore>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QPixmap>
#include <QFrame>
#include <QTimer>
#include <QWebEngineScript>
#include <QCloseEvent>

IncognitoHomePage::IncognitoHomePage(QWidget* parent) : QWidget(parent) {
    setStyleSheet("background: #0a0008;");
    auto* layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(20);

    auto* logo = new QLabel(this);
    QPixmap pix(":/sabre/icons/incognitologonobg.png");
    logo->setPixmap(pix.scaled(120, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo->setAlignment(Qt::AlignCenter);
    layout->addWidget(logo);

    auto* title = new QLabel("You're Incognito", this);
    title->setStyleSheet("color: white; font-family: 'GeistMono'; font-size: 22px;");
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto* subtitle = new QLabel("No history. No cookies. No trace.", this);
    subtitle->setStyleSheet("color: #888888; font-family: 'GeistMono'; font-size: 12px;");
    subtitle->setAlignment(Qt::AlignCenter);
    layout->addWidget(subtitle);

    urlBar = new QLineEdit(this);
    urlBar->setPlaceholderText("search or enter address");
    urlBar->setFixedWidth(480);
    urlBar->setStyleSheet(R"(
        QLineEdit {
            background: #1a1a1a; color: white; border: 1px solid #333;
            border-radius: 4px; padding: 8px; font-family: 'GeistMono';
        }
        QLineEdit:focus { border: 1px solid #ff2d2d; }
    )");
    layout->addWidget(urlBar, 0, Qt::AlignCenter);

    connect(urlBar, &QLineEdit::returnPressed, this, [this]() {
        emit navigateRequested(urlBar->text());
    });
}

IncognitoWindow::IncognitoWindow(FingerprintController* fingerprint,
                                 SearchController* searchController,
                                 QWidget* parent)
    : QMainWindow(parent),
      m_fingerprint(fingerprint),
      m_searchController(searchController)
{
    m_contextId = m_fingerprint->startIncognito();

    m_incognitoProfile = new QWebEngineProfile(this);
    m_incognitoProfile->setHttpUserAgent(
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/147.0.0.0 Safari/537.36"
    );

    AdblockInterceptor* adblock = new AdblockInterceptor(this);
    m_incognitoProfile->setUrlRequestInterceptor(adblock);

    QWebEngineScript fpScript;
    fpScript.setName("sabre-incognito");
    fpScript.setInjectionPoint(QWebEngineScript::DocumentCreation);
    fpScript.setWorldId(QWebEngineScript::MainWorld);
    fpScript.setRunsOnSubFrames(true);
    fpScript.setSourceCode(m_fingerprint->sessionScript(m_contextId));
    m_incognitoProfile->scripts()->insert(fpScript);

    setWindowTitle("Sabre — Incognito");
    resize(1280, 800);
    setStyleSheet("QMainWindow { background: #0a0008; }");

    setupUI();
    addTab("about://home");
}

void IncognitoWindow::setupUI() {
    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    setCentralWidget(central);

    m_topBar = new QWidget(central);
    m_topBar->setFixedHeight(50);
    m_topBar->setStyleSheet("background: #0a0008; border-bottom: 1px solid #1a1a1a;");
    auto* tbl = new QHBoxLayout(m_topBar);
    tbl->setContentsMargins(8, 6, 8, 0);
    tbl->setSpacing(4);

    auto* logoLabel = new QLabel(m_topBar);
    logoLabel->setPixmap(QIcon(":/sabre/icons/incognito.svg").pixmap(16, 16));
    logoLabel->setFixedSize(16, 16);
    tbl->addWidget(logoLabel);

    tbl->addSpacing(8);

    auto makeBtn = [&](const QString& iconPath, const QString& tooltip) {
        auto* btn = new QPushButton(m_topBar);
        btn->setIcon(QIcon(":/sabre/icons/" + iconPath));
        btn->setIconSize(QSize(15, 15));
        btn->setFixedSize(30, 30);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(tooltip);
        btn->setStyleSheet(R"(
            QPushButton { background: transparent; border: none; border-radius: 3px; }
            QPushButton:hover   { background: #1e1e1e; }
            QPushButton:pressed { background: #0a0a0a; }
        )");
        return btn;
    };

    auto* backBtn = makeBtn("back.svg", "Back");
    auto* fwdBtn = makeBtn("forward.svg", "Forward");
    auto* reloadBtn = makeBtn("reload.svg", "Reload");

    connect(backBtn, &QPushButton::clicked, this, [this]() { if (auto* t = currentTab()) t->goBack(); });
    connect(fwdBtn, &QPushButton::clicked, this, [this]() { if (auto* t = currentTab()) t->goForward(); });
    connect(reloadBtn, &QPushButton::clicked, this, [this]() { if (auto* t = currentTab()) t->reload(); });

    tbl->addWidget(backBtn);
    tbl->addWidget(fwdBtn);
    tbl->addWidget(reloadBtn);

    tbl->addSpacing(4);
    auto* sep = new QFrame(m_topBar);
    sep->setFrameShape(QFrame::VLine);
    sep->setFixedHeight(20);
    sep->setStyleSheet("color: #2a2a2a;");
    tbl->addWidget(sep);
    tbl->addSpacing(2);

    m_tabs = new SabreTabBar(m_topBar);
    m_tabs->setStyleSheet("background: #110011;");

    auto* newTabBtn = new QPushButton("+", m_topBar);
    newTabBtn->setFixedSize(30, 30);
    newTabBtn->setCursor(Qt::PointingHandCursor);
    newTabBtn->setStyleSheet(R"(
        QPushButton { background: transparent; color: #aaaaaa; border: 1px solid #2a2a2a; font-size: 18px; border-radius: 3px; }
        QPushButton:hover   { background: #1c1c1c; color: #ffffff; border-color: #3a3a3a; }
        QPushButton:pressed { background: #111111; }
    )");

    tbl->addWidget(m_tabs, 1);
    tbl->addWidget(newTabBtn);

    connect(m_tabs, &QTabBar::currentChanged, this, &IncognitoWindow::switchTab);
    connect(m_tabs, &QTabBar::tabCloseRequested, this, &IncognitoWindow::closeTab);
    connect(newTabBtn, &QPushButton::clicked, this, [this]() { addTab("about://home"); });

    m_pageStack = new QStackedWidget(central);

    m_incognitoHomePage = new IncognitoHomePage(m_pageStack);
    connect(m_incognitoHomePage, &IncognitoHomePage::navigateRequested, this, [this](const QString& url) {
        if (auto* tab = currentTab()) {
            tab->navigateTo(url);
        }
    });
    m_pageStack->addWidget(m_incognitoHomePage);

    m_stack = new QStackedWidget(m_pageStack);
    m_pageStack->addWidget(m_stack);

    m_adblockPage = new AdblockPage(m_pageStack);
    m_pageStack->addWidget(m_adblockPage);

    root->addWidget(m_topBar);
    root->addWidget(m_pageStack, 1);
}

void IncognitoWindow::addTab(const QString& url) {
    auto* tab = new BrowserTab(m_incognitoProfile, m_searchController, m_fingerprint, this);
    connect(tab, &BrowserTab::urlChanged, this, &IncognitoWindow::onTabUrlChanged);
    connect(tab, &BrowserTab::titleChanged, this, &IncognitoWindow::onTabTitleChanged);

    m_stack->addWidget(tab);
    m_tabs->addTab("New Tab");
    m_tabs->setCurrentIndex(m_stack->count() - 1);
    m_stack->setCurrentIndex(m_stack->count() - 1);

    QTimer::singleShot(0, tab, [tab, url]() { tab->navigateTo(url); });
}

void IncognitoWindow::closeTab(int index) {
    if (m_tabs->count() == 1) {
        close();
        return;
    }
    auto* widget = m_stack->widget(index);
    m_stack->removeWidget(widget);
    widget->deleteLater();
    m_tabs->removeTab(index);
}

void IncognitoWindow::switchTab(int index) {
    if (index < 0 || index >= m_stack->count()) return;
    m_stack->setCurrentIndex(index);
    if (auto* tab = currentTab()) {
        onTabUrlChanged(tab->currentUrl());
    }
}

BrowserTab* IncognitoWindow::currentTab() const {
    int idx = m_tabs->currentIndex();
    if (idx < 0) return nullptr;
    return qobject_cast<BrowserTab*>(m_stack->widget(idx));
}

void IncognitoWindow::onTabUrlChanged(const QString& url) {
    auto* tab = qobject_cast<BrowserTab*>(sender());
    if (!tab) return;

    int idx = m_stack->indexOf(tab);
    if (idx >= 0) {
        m_tabs->setTabText(idx, tab->currentTitle().isEmpty() ? "New Tab" : tab->currentTitle());
    }

    if (url == "about://home") {
        m_pageStack->setCurrentIndex(0);
        if (idx >= 0) m_tabs->setTabText(idx, "New Tab");
    } else if (url == "about://adblock") {
        m_pageStack->setCurrentIndex(2);
    } else {
        m_pageStack->setCurrentIndex(1);
    }
}

void IncognitoWindow::onTabTitleChanged(const QString& title) {
    auto* tab = qobject_cast<BrowserTab*>(sender());
    if (!tab) return;
    int idx = m_stack->indexOf(tab);
    if (idx >= 0) {
        m_tabs->setTabText(idx, title.isEmpty() ? "New Tab" : title);
    }
}

void IncognitoWindow::closeEvent(QCloseEvent* event) {
    m_fingerprint->endIncognito(m_contextId);
    m_incognitoProfile->clearAllVisitedLinks();
    m_incognitoProfile->cookieStore()->deleteAllCookies();
    event->accept();
}

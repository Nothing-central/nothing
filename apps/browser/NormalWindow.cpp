// ═══════════════════════════════════════════ NormalWindow.cpp ═════════════════════════════════════════
#include "NormalWindow.h"
#include "SearchController.h"
#include "AdblockPage.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QFontDatabase>
#include <QIcon>
#include <QSize>
#include <QSet>
#include <QFrame>
#include <QTimer>
#include <QVariantMap>
#include <QSplitter>
#include <QInputDialog>
#include <QMessageBox>

// ════════════════════════════════════════════════════════════════════════════════
//  Constructor
// ════════════════════════════════════════════════════════════════════════════════
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

// ════════════════════════════════════════════════════════════════════════════════
//  setupUI
// ════════════════════════════════════════════════════════════════════════════════
void NormalWindow::setupUI() {
    auto* central = new QWidget(this);
    auto* root    = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    setCentralWidget(central);

    // ── Top Bar ──────────────────────────────────────────────────────────────
    // Bumped from 42 → 50px so the close button has room to breathe
    m_topBar = new QWidget(central);
    m_topBar->setFixedHeight(50);
    m_topBar->setStyleSheet("background:#0c0c0c; border-bottom:1px solid #1a1a1a;");
    auto* tbl = new QHBoxLayout(m_topBar);
    tbl->setContentsMargins(8, 6, 8, 0);
    tbl->setSpacing(4);

    auto makeTabBtn = [&](const QString& iconPath, const QString& tooltip) {
        auto* btn = new QPushButton(m_topBar);
        btn->setIcon(QIcon(QString(SOURCE_DIR) + "/assets/icons/" + iconPath));
        btn->setIconSize(QSize(15, 15));
        btn->setFixedSize(30, 30);          // was 28×28
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(tooltip);
        btn->setStyleSheet(R"(
            QPushButton { background: transparent; border: none; border-radius: 3px; }
            QPushButton:hover   { background: #1e1e1e; }
            QPushButton:pressed { background: #0a0a0a; }
        )");
        return btn;
    };

    auto* profileBtn  = makeTabBtn("mainlogo-nobackground.png", "Profile");
    auto* settingsBtn = makeTabBtn("settings.svg", "Settings");
    connect(profileBtn,  &QPushButton::clicked, this, [this] { if (auto* t = currentTab()) t->navigateTo("about://profile"); });
    connect(settingsBtn, &QPushButton::clicked, this, [this] { if (auto* t = currentTab()) t->navigateTo("about://settings"); });

    tbl->addWidget(profileBtn);
    tbl->addWidget(settingsBtn);
    tbl->addSpacing(4);

    auto* sep = new QFrame(m_topBar);
    sep->setFrameShape(QFrame::VLine);
    sep->setFixedHeight(20);
    sep->setStyleSheet("color:#2a2a2a;");
    tbl->addWidget(sep);
    tbl->addSpacing(2);

    // ── Tab Bar ───────────────────────────────────────────────────────────────
    m_tabs = new SabreTabBar(m_topBar);

    auto* newTabBtn = new QPushButton("+", m_topBar);
    newTabBtn->setFixedSize(30, 30);        // was 28×28
    newTabBtn->setCursor(Qt::PointingHandCursor);
    newTabBtn->setStyleSheet(R"(
        QPushButton { background: transparent; color: #aaaaaa; border: 1px solid #2a2a2a; font-size: 18px; border-radius: 3px; }
        QPushButton:hover   { background: #1c1c1c; color: #ffffff; border-color:#3a3a3a; }
        QPushButton:pressed { background: #111111; }
    )");

    tbl->addWidget(m_tabs, 1);
    tbl->addWidget(newTabBtn);

    connect(m_tabs, &QTabBar::currentChanged,    this, &NormalWindow::switchTab);
    connect(m_tabs, &QTabBar::tabCloseRequested, this, &NormalWindow::closeTab);
    connect(newTabBtn, &QPushButton::clicked,    this, [this] { addTab("about://home"); });

    connect(m_tabs, &SabreTabBar::requestPin,         this, &NormalWindow::onTabPin);
    connect(m_tabs, &SabreTabBar::requestUnpin,       this, &NormalWindow::onTabUnpin);
    connect(m_tabs, &SabreTabBar::requestDuplicate,   this, &NormalWindow::onTabDuplicate);
    connect(m_tabs, &SabreTabBar::requestNewTabRight, this, &NormalWindow::onTabNewRight);
    connect(m_tabs, &SabreTabBar::requestNewTabLeft,  this, &NormalWindow::onTabNewLeft);
    connect(m_tabs, &SabreTabBar::requestMute,        this, &NormalWindow::onTabMute);
    connect(m_tabs, &SabreTabBar::requestUnmute,      this, &NormalWindow::onTabUnmute);
    connect(m_tabs, &SabreTabBar::requestReload,      this, &NormalWindow::onTabReload);
    connect(m_tabs, &SabreTabBar::requestSplitView,   this, &NormalWindow::onTabSplitView);

    // ── Page Stack ────────────────────────────────────────────────────────────
    m_pageStack = new QStackedWidget(central);

    m_stack = new QStackedWidget(m_pageStack);
    m_pageStack->addWidget(m_stack); // Index 0

    m_newTabPage = new NewTabPage(m_searchController, m_pageStack);
    connect(m_newTabPage, &NewTabPage::navigateRequested, this, [this](const QString& url) {
        static const QSet<QString> internal = {"about://home","about://profile","about://settings","about://downloads","about://adblock"};
        if (!internal.contains(url)) m_pageStack->setCurrentIndex(0);
        if (auto* tab = currentTab()) tab->navigateTo(url);
    });
    connect(m_newTabPage, &NewTabPage::showProfile,  this, [this] { if (auto* t = currentTab()) t->navigateTo("about://profile"); });
    connect(m_newTabPage, &NewTabPage::showSettings, this, [this] { if (auto* t = currentTab()) t->navigateTo("about://settings"); });
    m_pageStack->addWidget(m_newTabPage); // Index 1

    m_profilePage = new ProfilePage(m_fingerprint, m_pageStack);
    m_pageStack->addWidget(m_profilePage); // Index 2

    m_settingsPage = new SettingsPage(m_fingerprint, m_profile, m_lang, m_searchController, m_pageStack);
    connect(m_settingsPage, &SettingsPage::profileChanged,       this, [this](const QString&) { m_profilePage->refresh(); });
    connect(m_settingsPage, &SettingsPage::backgroundModeChanged, m_newTabPage, &NewTabPage::setBackgroundMode);
    if (m_searchController)
        connect(m_searchController, &SearchController::engineChanged, m_newTabPage, &NewTabPage::refreshEngineLabel);
    m_pageStack->addWidget(m_settingsPage); // Index 3

    m_noInternetPage = new NoInternetPage(m_pageStack);
    connect(m_noInternetPage, &NoInternetPage::retryRequested, this, [this] {
        m_pageStack->setCurrentIndex(0);
        if (auto* t = currentTab()) t->reload();
    });
    connect(m_noInternetPage, &NoInternetPage::playOfflineGame, this, [this] { m_pageStack->setCurrentIndex(5); });
    m_pageStack->addWidget(m_noInternetPage); // Index 4

    m_offlineGamePage = new OfflineGamePage(m_pageStack);
    connect(m_offlineGamePage, &OfflineGamePage::backRequested, this, [this] { m_pageStack->setCurrentIndex(4); });
    m_pageStack->addWidget(m_offlineGamePage); // Index 5

    // ── Adblock Page ──────────────────────────────────────────────────────────
    m_adblockPage = new AdblockPage(m_pageStack);
    m_pageStack->addWidget(m_adblockPage); // Index 6

    root->addWidget(m_topBar);
    root->addWidget(m_pageStack, 1);
}

// ════════════════════════════════════════════════════════════════════════════════
//  Tab creation
// ════════════════════════════════════════════════════════════════════════════════
void NormalWindow::addTab(const QString& url) {
    addTabAt(m_stack->count(), url);
}

void NormalWindow::addTabAt(int index, const QString& url) {
    auto* tab = new BrowserTab(m_profile, m_searchController, m_fingerprint, this);
    connect(tab, &BrowserTab::urlChanged,        this, &NormalWindow::onTabUrlChanged);
    connect(tab, &BrowserTab::titleChanged,       this, &NormalWindow::onTabTitleChanged);
    connect(tab, &BrowserTab::noInternetDetected, this, &NormalWindow::showNoInternetPage);

    m_stack->insertWidget(index, tab);
    m_tabs->insertTab(index, "New Tab");
    m_tabs->setCurrentIndex(index);
    m_stack->setCurrentIndex(index);
    updateTabData(index);

    QTimer::singleShot(0, tab, [tab, url]() { tab->navigateTo(url); });
}

// ════════════════════════════════════════════════════════════════════════════════
//  closeTab  — handles both normal tabs and split-group containers
// ════════════════════════════════════════════════════════════════════════════════
void NormalWindow::closeTab(int index) {
    // Is this tab a split-group container?
    if (SplitGroup* grp = splitGroupForTabIndex(index)) {
        teardownSplitGroup(grp);
        return;
    }

    if (m_tabs->count() == 1) { close(); return; }

    auto* widget = m_stack->widget(index);
    m_stack->removeWidget(widget);
    widget->deleteLater();
    m_tabs->removeTab(index);
}

// ════════════════════════════════════════════════════════════════════════════════
//  switchTab
// ════════════════════════════════════════════════════════════════════════════════
void NormalWindow::switchTab(int index) {
    if (index < 0 || index >= m_stack->count()) return;
    m_stack->setCurrentIndex(index);

    // If this is a split group, sync the url bar to the active pane
    if (SplitGroup* grp = splitGroupForTabIndex(index)) {
        BrowserTab* active = (grp->activePane == 0) ? grp->paneA : grp->paneB;
        if (active) onTabUrlChanged(active->currentUrl());
        return;
    }

    if (auto* tab = currentTab()) onTabUrlChanged(tab->currentUrl());
}

// ════════════════════════════════════════════════════════════════════════════════
//  currentTab — returns the *active* BrowserTab regardless of split state
// ════════════════════════════════════════════════════════════════════════════════
BrowserTab* NormalWindow::currentTab() const {
    int idx = m_tabs->currentIndex();
    if (idx < 0) return nullptr;

    if (SplitGroup* grp = splitGroupForTabIndex(idx)) {
        return (grp->activePane == 0) ? grp->paneA : grp->paneB;
    }

    return qobject_cast<BrowserTab*>(m_stack->widget(idx));
}

// ════════════════════════════════════════════════════════════════════════════════
//  showNoInternetPage
// ════════════════════════════════════════════════════════════════════════════════
void NormalWindow::showNoInternetPage() {
    m_pageStack->setCurrentIndex(4);
    int idx = m_tabs->currentIndex();
    if (idx >= 0) m_tabs->setTabText(idx, "No Connection");
}

void NormalWindow::showInternalPage(const QString& url) {
    if (url == "about://home")           m_pageStack->setCurrentIndex(1);
    else if (url == "about://profile")  { m_profilePage->refresh(); m_pageStack->setCurrentIndex(2); }
    else if (url == "about://settings")  m_pageStack->setCurrentIndex(3);
    else if (url == "about://downloads") m_pageStack->setCurrentIndex(0);
    else if (url == "about://adblock")   m_pageStack->setCurrentIndex(6);
}

// ════════════════════════════════════════════════════════════════════════════════
//  updateTabData
// ════════════════════════════════════════════════════════════════════════════════
void NormalWindow::updateTabData(int index) {
    auto* tab = qobject_cast<BrowserTab*>(m_stack->widget(index));
    if (!tab) return;
    QMap<QString, QVariant> data;
    data["pinned"] = tab->isPinned();
    data["muted"]  = tab->isMuted();
    m_tabs->setTabData(index, data);
}

// ════════════════════════════════════════════════════════════════════════════════
//  Split group helpers
// ════════════════════════════════════════════════════════════════════════════════
SplitGroup* NormalWindow::splitGroupForTabIndex(int index) const {
    for (SplitGroup* grp : m_splitGroups)
        if (grp->mainTabIndex == index) return grp;
    return nullptr;
}

void NormalWindow::updateSplitTabLabel(SplitGroup* grp) {
    if (!grp || grp->mainTabIndex < 0) return;

    QString titleA = grp->paneA ? grp->paneA->currentTitle() : QString();
    QString titleB = grp->paneB ? grp->paneB->currentTitle() : QString();
    if (titleA.isEmpty()) titleA = "New Tab";
    if (titleB.isEmpty()) titleB = "New Tab";

    // Truncate each pane title so combined label stays sane
    auto trunc = [](const QString& s, int n) {
        return s.length() > n ? s.left(n) + "…" : s;
    };

    // Active pane gets highlighted with brackets, inactive is dimmer
    QString label;
    if (grp->activePane == 0)
        label = QString("⊞ [%1] · %2").arg(trunc(titleA, 14), trunc(titleB, 14));
    else
        label = QString("⊞ %1 · [%2]").arg(trunc(titleA, 14), trunc(titleB, 14));

    m_tabs->setTabText(grp->mainTabIndex, label);
    m_tabs->setTabToolTip(grp->mainTabIndex,
        QString("Split: %1  |  %2\nClick a pane to make it active").arg(titleA, titleB));
}

void NormalWindow::teardownSplitGroup(SplitGroup* grp) {
    if (!grp) return;

    // Put the first pane back as a normal standalone tab in the same slot
    int idx = grp->mainTabIndex;

    // Remove the container widget from m_stack (it's the QSplitter)
    m_stack->removeWidget(grp->container);

    // Promote pane A to a regular tab in the same position
    if (grp->paneA) {
        m_stack->insertWidget(idx, grp->paneA);
        QString title = grp->paneA->currentTitle();
        m_tabs->setTabText(idx, title.isEmpty() ? "New Tab" : title);
        m_tabs->setTabToolTip(idx, "");
        m_stack->setCurrentIndex(idx);
    }

    // Pane B becomes a new tab right after
    if (grp->paneB) {
        m_stack->insertWidget(idx + 1, grp->paneB);
        m_tabs->insertTab(idx + 1, grp->paneB->currentTitle().isEmpty() ? "New Tab" : grp->paneB->currentTitle());
    }

    // The splitter container itself (without the panes, since we reparented them)
    grp->container->deleteLater();

    m_splitGroups.removeOne(grp);
    delete grp;

    m_tabs->setCurrentIndex(idx);
}

// ════════════════════════════════════════════════════════════════════════════════
//  onTabSplitView — THE main event. Tab B → B(i) B(ii) inline in the tab bar.
// ════════════════════════════════════════════════════════════════════════════════
void NormalWindow::onTabSplitView(int index) {
    // Don't split an already-split tab
    if (splitGroupForTabIndex(index)) return;

    auto* existingTab = qobject_cast<BrowserTab*>(m_stack->widget(index));
    if (!existingTab) return;

    // ── Ask the user: combine with an existing tab, or open new home page? ──
    //
    // Build a list of other tabs the user could merge with
    QStringList choices;
    QList<int>  choiceIndices;

    for (int i = 0; i < m_tabs->count(); ++i) {
        if (i == index) continue;
        if (splitGroupForTabIndex(i)) continue; // skip already-split groups
        choices << QString("[%1] %2").arg(i + 1).arg(m_tabs->tabText(i));
        choiceIndices << i;
    }

    choices << "Open a new home page in split pane";

    bool ok = false;
    QString chosen = QInputDialog::getItem(
        this,
        "Split View",
        QString("Split \"%1\" with:").arg(existingTab->currentTitle().isEmpty() ? "this tab" : existingTab->currentTitle()),
        choices, 0, false, &ok
    );

    if (!ok) return;

    // ── Create the QSplitter container ───────────────────────────────────────
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(3);
    splitter->setStyleSheet(R"(
        QSplitter::handle         { background: #1a1a1a; }
        QSplitter::handle:hover   { background: #ff2d2d; }
        QSplitter::handle:pressed { background: #cc0000; }
    )");
    splitter->setChildrenCollapsible(false);

    // ── Pane A: the original tab ─────────────────────────────────────────────
    BrowserTab* paneA = existingTab;

    // ── Pane B: either an existing tab or a brand-new one ────────────────────
    BrowserTab* paneB = nullptr;
    int chosenChoiceIdx = choices.indexOf(chosen);
    bool isNewPage = (chosen == "Open a new home page in split pane");

    if (!isNewPage && chosenChoiceIdx >= 0 && chosenChoiceIdx < choiceIndices.size()) {
        int otherIndex = choiceIndices[chosenChoiceIdx];

        // Pull that tab out of m_stack and m_tabs
        paneB = qobject_cast<BrowserTab*>(m_stack->widget(otherIndex));
        if (paneB) {
            m_stack->removeWidget(paneB);
            m_tabs->removeTab(otherIndex);
            // If the removed tab was before `index`, our index shifted down
            if (otherIndex < index) index--;
        }
    }

    if (!paneB) {
        paneB = new BrowserTab(m_profile, m_searchController, m_fingerprint, this);
        connect(paneB, &BrowserTab::urlChanged,        this, &NormalWindow::onTabUrlChanged);
        connect(paneB, &BrowserTab::titleChanged,       this, &NormalWindow::onTabTitleChanged);
        connect(paneB, &BrowserTab::noInternetDetected, this, &NormalWindow::showNoInternetPage);
        QTimer::singleShot(0, paneB, [paneB]() { paneB->navigateTo("about://home"); });
    }

    // ── Wire pane focus: clicking inside a pane makes it "active" ────────────
    // We detect this via the url/title changed signals (sender() trick)
    // and update grp->activePane accordingly. We set this up after grp exists.

    // ── Remove paneA from m_stack (we'll replace its slot with the splitter) ─
    m_stack->removeWidget(paneA);

    // ── Build the group ───────────────────────────────────────────────────────
    auto* grp        = new SplitGroup;
    grp->mainTabIndex = index;
    grp->container   = splitter;
    grp->paneA       = paneA;
    grp->paneB       = paneB;
    grp->activePane  = 0;
    m_splitGroups.append(grp);

    // Add both panes to the splitter
    splitter->addWidget(paneA);
    splitter->addWidget(paneB);
    splitter->setSizes({ width() / 2, width() / 2 });

    // Replace the tab's widget in m_stack with the splitter
    m_stack->insertWidget(index, splitter);
    m_stack->setCurrentIndex(index);
    m_tabs->setCurrentIndex(index);

    // ── Active-pane tracking ─────────────────────────────────────────────────
    // When pane A's URL or title changes, mark it active and refresh label
    connect(paneA, &BrowserTab::urlChanged, this, [this, grp](const QString&) {
        grp->activePane = 0;
        updateSplitTabLabel(grp);
    });
    connect(paneA, &BrowserTab::titleChanged, this, [this, grp](const QString&) {
        grp->activePane = 0;
        updateSplitTabLabel(grp);
    });
    connect(paneB, &BrowserTab::urlChanged, this, [this, grp](const QString&) {
        grp->activePane = 1;
        updateSplitTabLabel(grp);
    });
    connect(paneB, &BrowserTab::titleChanged, this, [this, grp](const QString&) {
        grp->activePane = 1;
        updateSplitTabLabel(grp);
    });

    // Set the initial label
    updateSplitTabLabel(grp);
}

// ════════════════════════════════════════════════════════════════════════════════
//  Standard tab management slots (unchanged logic, just cleaner)
// ════════════════════════════════════════════════════════════════════════════════
void NormalWindow::onTabPin(int index) {
    auto* tab = qobject_cast<BrowserTab*>(m_stack->widget(index));
    if (!tab) return;
    tab->setPinned(true);
    m_tabs->setTabText(index, " ");
    m_tabs->setTabIcon(index, tab->webView()->icon().isNull()
        ? QIcon(QString(SOURCE_DIR) + "/assets/icons/globe.svg") : tab->webView()->icon());
    m_tabs->setTabToolTip(index, tab->currentTitle());
    m_tabs->moveTab(index, 0);
    updateTabData(0);
}

void NormalWindow::onTabUnpin(int index) {
    auto* tab = qobject_cast<BrowserTab*>(m_stack->widget(index));
    if (!tab) return;
    tab->setPinned(false);
    m_tabs->setTabText(index, tab->currentTitle());
    m_tabs->setTabToolTip(index, "");
    m_tabs->setTabIcon(index, QIcon());
    updateTabData(index);
}

void NormalWindow::onTabDuplicate(int index) {
    auto* tab = qobject_cast<BrowserTab*>(m_stack->widget(index));
    if (!tab) return;
    addTabAt(index + 1, tab->currentUrl());
}

void NormalWindow::onTabNewRight(int index) { addTabAt(index + 1, "about://home"); }
void NormalWindow::onTabNewLeft(int index)  { addTabAt(index,     "about://home"); }

void NormalWindow::onTabMute(int index) {
    auto* tab = qobject_cast<BrowserTab*>(m_stack->widget(index));
    if (!tab) return;
    tab->setMuted(true);
    m_tabs->setTabIcon(index, QIcon(QString(SOURCE_DIR) + "/assets/icons/mute.svg"));
    updateTabData(index);
}

void NormalWindow::onTabUnmute(int index) {
    auto* tab = qobject_cast<BrowserTab*>(m_stack->widget(index));
    if (!tab) return;
    tab->setMuted(false);
    m_tabs->setTabIcon(index, tab->webView()->icon().isNull()
        ? QIcon(QString(SOURCE_DIR) + "/assets/icons/globe.svg") : tab->webView()->icon());
    updateTabData(index);
}

void NormalWindow::onTabReload(int index) {
    // Handle split group reload (reloads active pane)
    if (SplitGroup* grp = splitGroupForTabIndex(index)) {
        BrowserTab* active = (grp->activePane == 0) ? grp->paneA : grp->paneB;
        if (active) active->reload();
        return;
    }
    auto* tab = qobject_cast<BrowserTab*>(m_stack->widget(index));
    if (tab) tab->reload();
}

// ════════════════════════════════════════════════════════════════════════════════
//  URL / Title change propagation
// ════════════════════════════════════════════════════════════════════════════════
void NormalWindow::onTabUrlChanged(const QString& url) {
    auto* tab = qobject_cast<BrowserTab*>(sender());
    if (!tab) return;

    static const QSet<QString> internal = {
        "about://home", "about://profile", "about://settings", "about://downloads", "about://adblock"
    };
    if (url.isEmpty() || url == "about:blank") return;

    // Check if this tab belongs to a split group
    for (SplitGroup* grp : m_splitGroups) {
        if (grp->paneA == tab || grp->paneB == tab) {
            // Internal pages in a split pane: just handle the label, don't fullscreen
            if (internal.contains(url)) {
                // Show the internal page only if this is the active pane and tab is selected
                if (m_tabs->currentIndex() == grp->mainTabIndex) {
                    if ((grp->paneA == tab && grp->activePane == 0) ||
                        (grp->paneB == tab && grp->activePane == 1)) {
                        showInternalPage(url);
                    }
                }
            } else {
                m_pageStack->setCurrentIndex(0);
            }
            updateSplitTabLabel(grp);
            return;
        }
    }

    // Regular tab
    if (internal.contains(url)) showInternalPage(url);
    else m_pageStack->setCurrentIndex(0);

    int idx = m_stack->indexOf(tab);
    if (idx >= 0 && internal.contains(url)) {
        QString label = (url == "about://home")      ? "New Tab"
                      : (url == "about://profile")   ? "Profile"
                      : (url == "about://settings")  ? "Settings"
                      : (url == "about://downloads") ? "Downloads"
                      : (url == "about://adblock")   ? "Adblock"
                      : "New Tab";
        m_tabs->setTabText(idx, label);
    }
}

void NormalWindow::onTabTitleChanged(const QString& title) {
    auto* tab = qobject_cast<BrowserTab*>(sender());
    if (!tab || title.isEmpty()) return;

    // Check split groups first
    for (SplitGroup* grp : m_splitGroups) {
        if (grp->paneA == tab || grp->paneB == tab) {
            updateSplitTabLabel(grp);
            return;
        }
    }

    // Regular tab
    int idx = m_stack->indexOf(tab);
    if (idx >= 0 && !m_tabs->tabData(idx).toMap().value("pinned", false).toBool())
        m_tabs->setTabText(idx, title.length() > 28 ? title.left(28) + "…" : title);
}

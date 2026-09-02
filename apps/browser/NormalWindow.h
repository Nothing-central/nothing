// ═══════════════════════════════════════════ NormalWindow.h ═══════════════════════════════════════════
#pragma once
#include <QMainWindow>
#include <QStackedWidget>
#include <QtWebEngineCore/QWebEngineProfile>
#include <QSplitter>
#include "BrowserTab.h"
#include "FingerprintController.h"
#include "NewTabPage.h"
#include "NoInternetPage.h"
#include "OfflineGamePage.h"
#include "ProfilePage.h"
#include "SettingsPage.h"
#include "LangManager.h"
#include "SabreTabBar.h"

class SearchController;
class AdblockPage;

// ── SplitGroup ────────────────────────────────────────────────────────────────
// Represents a main tab that has been split into sub-panes (i) and (ii).
// Lives entirely inside m_stack / m_tabs — no second tab bar anywhere.
struct SplitGroup {
    int      mainTabIndex  = -1;   // index in m_tabs of the "parent" slot
    QWidget* container     = nullptr; // QSplitter that lives in m_stack
    // Each pane: a BrowserTab* inside the splitter
    BrowserTab* paneA      = nullptr;
    BrowserTab* paneB      = nullptr;
    // Which pane is currently "active" (receives keyboard / url-bar focus)
    int activePane = 0; // 0 = left, 1 = right
};

class NormalWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit NormalWindow(QWebEngineProfile* profile,
                          FingerprintController* fingerprint,
                          LangManager* lang,
                          SearchController* searchController,
                          QWidget* parent = nullptr);
    void addTab(const QString& url);
    void addTabAt(int index, const QString& url);
    void showNoInternetPage();

private slots:
    void closeTab(int index);
    void switchTab(int index);
    void onTabUrlChanged(const QString& url);
    void onTabTitleChanged(const QString& title);

    // Tab management
    void onTabPin(int index);
    void onTabUnpin(int index);
    void onTabDuplicate(int index);
    void onTabNewRight(int index);
    void onTabNewLeft(int index);
    void onTabMute(int index);
    void onTabUnmute(int index);
    void onTabReload(int index);
    void onTabSplitView(int index);
    void updateTabData(int index);

private:
    void setupUI();
    void showInternalPage(const QString& url);
    BrowserTab* currentTab() const;

    // Split group helpers
    SplitGroup* splitGroupForTabIndex(int index) const;
    void        teardownSplitGroup(SplitGroup* grp);
    void        updateSplitTabLabel(SplitGroup* grp);

    AdblockPage* m_adblockPage = nullptr;
    QWebEngineProfile*     m_profile          = nullptr;
    FingerprintController* m_fingerprint      = nullptr;
    LangManager*           m_lang             = nullptr;
    SearchController*      m_searchController = nullptr;

    QWidget*               m_topBar           = nullptr;
    SabreTabBar*           m_tabs             = nullptr;
    QStackedWidget*        m_pageStack        = nullptr;
    QStackedWidget*        m_stack            = nullptr;

    NewTabPage*            m_newTabPage       = nullptr;
    ProfilePage*           m_profilePage      = nullptr;
    SettingsPage*          m_settingsPage     = nullptr;
    NoInternetPage*        m_noInternetPage   = nullptr;
    OfflineGamePage*       m_offlineGamePage  = nullptr;

    // All active split groups (usually 0 or 1, but the model supports many)
    QList<SplitGroup*>     m_splitGroups;
};

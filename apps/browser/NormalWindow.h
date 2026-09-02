// ═══════════════════════════════════════════ NormalWindow.h ═══════════════════════════════════════════
#pragma once
#include <QMainWindow>
#include <QTabBar>
#include <QStackedWidget>
#include <QtWebEngineCore/QWebEngineProfile>
#include "BrowserTab.h"
#include "FingerprintController.h"
#include "NewTabPage.h"
#include "NoInternetPage.h"      // ← NEW
#include "OfflineGamePage.h"     // ← NEW
#include "ProfilePage.h"
#include "SettingsPage.h"
#include "LangManager.h"

class SearchController;

class NormalWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit NormalWindow(QWebEngineProfile* profile,
                          FingerprintController* fingerprint,
                          LangManager* lang,
                          SearchController* searchController,
                          QWidget* parent = nullptr);

    void addTab(const QString& url);

    // Called by BrowserTab when a network error is detected
    void showNoInternetPage();

private slots:
    void closeTab(int index);
    void switchTab(int index);
    void onTabUrlChanged(const QString& url);
    void onTabTitleChanged(const QString& title);

private:
    void setupUI();
    void showInternalPage(const QString& url);
    BrowserTab* currentTab() const;

    QWebEngineProfile*     m_profile          = nullptr;
    FingerprintController* m_fingerprint      = nullptr;
    LangManager*           m_lang             = nullptr;
    SearchController*      m_searchController = nullptr;
    QWidget*               m_tabBar           = nullptr;
    QTabBar*               m_tabs             = nullptr;
    QStackedWidget*        m_pageStack        = nullptr;
    QStackedWidget*        m_stack            = nullptr;
    NewTabPage*            m_newTabPage       = nullptr;
    ProfilePage*           m_profilePage      = nullptr;
    SettingsPage*          m_settingsPage     = nullptr;
    NoInternetPage*        m_noInternetPage   = nullptr;   // ← NEW  (index 4)
    OfflineGamePage*       m_offlineGamePage  = nullptr;   // ← NEW  (index 5)
};

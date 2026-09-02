#pragma once
#include <QMainWindow>
#include <QStackedWidget>
#include <QWebEngineProfile>
#include <QString>
#include <QLineEdit>

class FingerprintController;
class SearchController;
class SabreTabBar;
class BrowserTab;
class AdblockPage;
class QCloseEvent;

class IncognitoHomePage : public QWidget {
    Q_OBJECT
public:
    explicit IncognitoHomePage(QWidget* parent = nullptr);
    QLineEdit* urlBar;
signals:
    void navigateRequested(const QString& url);
};

class IncognitoWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit IncognitoWindow(FingerprintController* fingerprint,
                             SearchController* searchController,
                             QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void addTab(const QString& url = "about://home");
    void closeTab(int index);
    void switchTab(int index);
    void onTabUrlChanged(const QString& url);
    void onTabTitleChanged(const QString& title);

private:
    void setupUI();
    BrowserTab* currentTab() const;

    FingerprintController* m_fingerprint;
    SearchController*      m_searchController;
    QWebEngineProfile*     m_incognitoProfile;
    QString                m_contextId;

    QWidget*               m_topBar;
    SabreTabBar*           m_tabs;
    QStackedWidget*        m_pageStack;
    QStackedWidget*        m_stack;
    IncognitoHomePage*     m_incognitoHomePage;
    AdblockPage*           m_adblockPage;
};

// ═══════════════════════════════════════════ BrowserTab.h ═════════════════════════════════════════════
#pragma once
#include <QWidget>
#include <QWebEngineProfile>
#include <QWebEngineView>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QIcon>
#include <QSize>

class SearchController;
class FingerprintController;

// One browser tab — owns its QWebEngineView and knows nothing about QML.
// The fingerprint profile is passed in from main so all tabs in a window
// share the same injected identity.
class BrowserTab : public QWidget {
    Q_OBJECT
public:
    explicit BrowserTab(QWebEngineProfile* profile, SearchController* searchController,
                        FingerprintController* fingerprint, QWidget* parent = nullptr);

    QString currentUrl()   const;
    QString currentTitle() const;
    bool    canGoBack()    const;
    bool    canGoForward() const;

public slots:
    void navigateTo(const QString& url);
    void goBack();
    void goForward();
    void reload();
    void stop();

signals:
    void titleChanged(const QString& title);
    void urlChanged(const QString& url);
    void loadingChanged(bool loading);
    void loadProgressChanged(int progress);
    void iconChanged();

private slots:
    void onNavigateRequested();
    void onUrlChanged(const QUrl& url);
    void onTitleChanged(const QString& title);
    void onLoadStarted();
    void onLoadFinished(bool ok);
    void onLoadProgress(int progress);

private:
    void setupUI();
    void setupProfile(QWebEngineProfile* profile);
    QString resolveInput(const QString& raw);
    void updateNavButtons();

    SearchController*      m_searchController = nullptr;
    FingerprintController* m_fingerprint       = nullptr;

    QWebEngineView* m_view       = nullptr;
    QLineEdit*      m_urlBar     = nullptr;
    QPushButton*    m_backBtn    = nullptr;
    QPushButton*    m_fwdBtn     = nullptr;
    QPushButton*    m_reloadBtn  = nullptr;
    QProgressBar*   m_progress   = nullptr;
    QLabel*         m_secIcon    = nullptr;
    bool            m_loading    = false;
    QString         m_currentLogicalUrl;
};
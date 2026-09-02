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
#include <QTimer>
#include <QWebChannel>
#include <QUrl>
#include <QStackedWidget>
#include <QWebEngineLoadingInfo>
#include <QWebEngineScriptCollection>

class SearchController;
class FingerprintController;
class FormRecoveryController;
class FossilCacheManager;
class FossilBridge;
class DownloadPanel;
class DownloadsPage;
class QWebEngineLoadingInfo;

class BrowserTab : public QWidget {
    Q_OBJECT
public:
    explicit BrowserTab(QWebEngineProfile* profile, SearchController* searchController,
                        FingerprintController* fingerprint, QWidget* parent = nullptr);

    QString currentUrl()   const;
    QString currentTitle() const;
    bool    canGoBack()    const;
    bool    canGoForward() const;

    // ── NEW: Tab State ──
    bool isPinned() const;
    void setPinned(bool pinned);
    bool isMuted()  const;
    void setMuted(bool muted);
    QWebEngineView* webView() const; // Expose for split view

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
    void noInternetDetected();

private slots:
    void onNavigateRequested();
    void onUrlChanged(const QUrl& url);
    void onTitleChanged(const QString& title);
    void onLoadStarted();
    void onLoadFinished(bool ok);
    void onLoadProgress(int progress);
    void performRetry();
    void onFossilizeRequested();
    void onAssetCollectionFinished();
    void onHeuristicReported(const QString& status);
    void toggleDownloadPanel();

private:
    void setupUI();
    void setupProfile(QWebEngineProfile* profile);
    QString resolveInput(const QString& raw);
    void updateNavButtons();
    bool isRetryableError(const QWebEngineLoadingInfo& info) const;
    void scheduleRetry(const QUrl& url);
    void cancelRetry();
    void setupFormRecovery();
    void setupFossilCache();
    void injectHeuristic();

    SearchController*       m_searchController = nullptr;
    FingerprintController*  m_fingerprint      = nullptr;
    QWebEngineView*         m_view             = nullptr;
    QLineEdit*              m_urlBar           = nullptr;

    QPushButton*            m_backBtn          = nullptr;
    QPushButton*            m_fwdBtn           = nullptr;
    QPushButton*            m_reloadBtn        = nullptr;
    QPushButton*            m_fossilBtn        = nullptr;
    QPushButton*            m_downloadBtn      = nullptr;

    QProgressBar*           m_progress         = nullptr;
    QLabel*                 m_secIcon          = nullptr;
    QLabel*                 m_leafIcon         = nullptr;

    bool                    m_loading          = false;
    bool                    m_pinned           = false;  // NEW
    bool                    m_muted            = false;  // NEW
    QString                 m_currentLogicalUrl;
    QTimer*                 m_retryTimer       = nullptr;
    int                     m_retryCount       = 0;
    QUrl                    m_pendingRetryUrl;

    FormRecoveryController* m_formRecovery     = nullptr;
    QWebChannel*            m_webChannel       = nullptr;
    FossilCacheManager*     m_fossilManager    = nullptr;
    FossilBridge*           m_fossilBridge     = nullptr;
    QString                 m_currentFossilId;

    QStackedWidget*         m_stack            = nullptr;
    DownloadPanel*          m_downloadPanel    = nullptr;
    DownloadsPage*          m_downloadsPage    = nullptr;
};

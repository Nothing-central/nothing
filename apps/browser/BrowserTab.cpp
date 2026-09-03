// ═══════════════════════════════════════════ BrowserTab.cpp ═══════════════════════════════════════════
#include "BrowserTab.h"
#include "SearchController.h"
#include "FingerprintController.h"
#include "FormRecoveryController.h"
#include "FossilCacheManager.h"
#include "FossilBridge.h"
#include "DownloadManager.h"
#include "DownloadPanel.h"
#include "DownloadsPage.h"

#include <QVBoxLayout>
#include <QWebEngineLoadingInfo>
#include <QWebEngineScriptCollection>
#include <QHBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineHistory>
#include <QWebEngineScript>
#include <QWebEngineDownloadRequest>
#include <QFile>
#include <QDir>
#include <QUuid>
#include <QDebug>

class SabrePage : public QWebEnginePage {
    Q_OBJECT
public:
    using QWebEnginePage::QWebEnginePage;
signals:
    void internalNavigation(const QString& url);
protected:
    bool acceptNavigationRequest(const QUrl& url, NavigationType type, bool isMainFrame) override {
        if (!isMainFrame) return true;
        QString s = url.toString();
        if (url.scheme() == "about" && (s == "about://home" || s == "about://profile" || s == "about://settings" || s == "about://fossils" || s == "about://downloads" || s == "about://adblock")) {
            emit internalNavigation(s);
            return false;
        }
        return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
    }
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel, const QString& msg, int line, const QString& src) override {
        qDebug().noquote() << "[JS]" << msg << "(" << src << ":" << line << ")";
    }
};

BrowserTab::BrowserTab(QWebEngineProfile* profile, SearchController* searchController,
                       FingerprintController* fingerprint, QWidget* parent)
    : QWidget(parent), m_searchController(searchController), m_fingerprint(fingerprint)
{
    setupUI();
    setupProfile(profile);

    m_retryTimer = new QTimer(this);
    m_retryTimer->setSingleShot(true);
    connect(m_retryTimer, &QTimer::timeout, this, &BrowserTab::performRetry);

    m_formRecovery = FormRecoveryController::instance();
    m_fossilManager = FossilCacheManager::instance();
    m_fossilBridge = new FossilBridge(m_fossilManager, this);
    connect(m_fossilBridge, &FossilBridge::assetCollectionFinished, this, &BrowserTab::onAssetCollectionFinished);
    connect(m_fossilBridge, &FossilBridge::heuristicReported, this, &BrowserTab::onHeuristicReported);

    m_webChannel = new QWebChannel(this);
    m_webChannel->registerObject("formRecovery", m_formRecovery);
    m_webChannel->registerObject("fossilBridge", m_fossilBridge);
}

void BrowserTab::setupUI() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* toolbar = new QWidget(this);
    toolbar->setFixedHeight(46);
    toolbar->setStyleSheet("background:#111111; border-bottom:1px solid #1c1c1c;");

    auto* tl = new QHBoxLayout(toolbar);
    tl->setContentsMargins(8, 6, 8, 6);
    tl->setSpacing(4);

    auto makeBtn = [&](const QString& iconPath) {
        auto* btn = new QPushButton(toolbar);
        btn->setIcon(QIcon(":/sabre/icons/" + iconPath));
        btn->setIconSize(QSize(18, 18));
        btn->setFixedSize(32, 32);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(R"(
            QPushButton { background: transparent; border: none; border-radius: 2px; }
            QPushButton:hover   { background:#1e1e1e; }
            QPushButton:pressed { background:#0a0a0a; }
        )");
        return btn;
    };

    m_backBtn    = makeBtn("back.svg");
    m_fwdBtn     = makeBtn("forward.svg");
    m_reloadBtn  = makeBtn("reload.svg");
    m_fossilBtn  = makeBtn("fossil.svg");
    m_downloadBtn = makeBtn("download.svg");

    m_fossilBtn->setEnabled(false);
    m_fossilBtn->setToolTip("Save this page for offline reading");
    m_downloadBtn->setToolTip("Downloads");

    m_backBtn->setEnabled(false);
    m_fwdBtn->setEnabled(false);

    m_secIcon = new QLabel("◈ ", toolbar);
    m_secIcon->setStyleSheet("color:#ff2d2d; font-size:9px; padding:0 4px;");

    m_leafIcon = new QLabel("", toolbar);
    m_leafIcon->setStyleSheet("font-size:14px; padding:0 4px;");

    m_urlBar = new QLineEdit(toolbar);
    m_urlBar->setPlaceholderText("search or enter address");
    m_urlBar->setStyleSheet(R"(
        QLineEdit { background: #161616; color: #cccccc; border: 1px solid #262626; border-radius: 2px; padding: 4px 10px; font-family: "GeistMono", monospace; font-size: 11px; }
        QLineEdit:focus { border-color: #3a3a3a; color: #ffffff; }
    )");

    m_progress = new QProgressBar(this);
    m_progress->setFixedHeight(1);
    m_progress->setTextVisible(false);
    m_progress->setStyleSheet("QProgressBar { background:transparent; border:none; } QProgressBar::chunk { background:#ff2d2d; }");
    m_progress->hide();

    tl->addWidget(m_backBtn);
    tl->addWidget(m_fwdBtn);
    tl->addWidget(m_reloadBtn);
    tl->addWidget(m_fossilBtn);
    tl->addWidget(m_downloadBtn);
    tl->addWidget(m_secIcon);
    tl->addWidget(m_leafIcon);
    tl->addWidget(m_urlBar, 1);

    connect(m_downloadBtn, &QPushButton::clicked, this, &BrowserTab::toggleDownloadPanel);

    m_view = new QWebEngineView(this);
    m_view->setStyleSheet("background:#0e0e0e;");

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(m_view);

    root->addWidget(toolbar);
    root->addWidget(m_progress);
    root->addWidget(m_stack, 1);

    connect(m_backBtn,    &QPushButton::clicked, this, &BrowserTab::goBack);
    connect(m_fwdBtn,     &QPushButton::clicked, this, &BrowserTab::goForward);
    connect(m_reloadBtn,  &QPushButton::clicked, this, [this] { cancelRetry(); m_loading ? m_view->stop() : m_view->reload(); });
    connect(m_fossilBtn,  &QPushButton::clicked, this, &BrowserTab::onFossilizeRequested);
    connect(m_urlBar,     &QLineEdit::returnPressed, this, &BrowserTab::onNavigateRequested);
}

void BrowserTab::setupProfile(QWebEngineProfile* profile) {
    auto* page = new SabrePage(profile, m_view);
    m_view->setPage(page);

    connect(page, &SabrePage::internalNavigation, this, [this](const QString& url) {
        m_currentLogicalUrl = url;
        emit urlChanged(url);
        m_urlBar->setText(url);

        if (url == "about://fossils") {
            m_stack->setCurrentWidget(m_view);
            m_view->setHtml(R"(<body style='background:#0e0e0e; color:#ccc; font-family:sans-serif; padding:40px;'><h1>🦴 Fossil Cache</h1><p>Your downloaded offline pages will appear here.</p></body>)", QUrl("about://fossils"));
        }
        else if (url == "about://downloads") {
            if (!m_downloadsPage) {
                m_downloadsPage = new DownloadsPage(this);
                m_stack->addWidget(m_downloadsPage);
            }
            m_stack->setCurrentWidget(m_downloadsPage);
        }
        else {
            m_stack->setCurrentWidget(m_view);
        }
    });

    connect(m_view->page(), &QWebEnginePage::loadingChanged, this, [this](const QWebEngineLoadingInfo& info) {
        using S = QWebEngineLoadingInfo;
        if (info.status() == S::LoadFailedStatus) {
            QString scheme = info.url().scheme();
            if (scheme == "http" || scheme == "https") {
                if (isRetryableError(info)) scheduleRetry(info.url());
                else { cancelRetry(); emit noInternetDetected(); }
            }
        } else if (info.status() == S::LoadSucceededStatus) {
            cancelRetry();
            injectHeuristic();
        }
    });

    connect(m_view, &QWebEngineView::urlChanged,   this, &BrowserTab::onUrlChanged);
    connect(m_view, &QWebEngineView::titleChanged,  this, &BrowserTab::onTitleChanged);
    connect(m_view, &QWebEngineView::loadStarted,   this, &BrowserTab::onLoadStarted);
    connect(m_view, &QWebEngineView::loadFinished,  this, &BrowserTab::onLoadFinished);
    connect(m_view, &QWebEngineView::loadProgress,  this, &BrowserTab::onLoadProgress);

    auto* s = m_view->settings();
    s->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    s->setAttribute(QWebEngineSettings::WebGLEnabled, true);
    s->setAttribute(QWebEngineSettings::Accelerated2dCanvasEnabled, true);
    s->setAttribute(QWebEngineSettings::AutoLoadImages, true);
    s->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, false);

    setupFormRecovery();
    setupFossilCache();
}

void BrowserTab::setupFormRecovery() {
    m_view->page()->setWebChannel(m_webChannel);
    QFile channelFile(":/sabre/qwebchannel.js");
    QString channelJs = channelFile.open(QFile::ReadOnly) ? QString(channelFile.readAll()) : QString();
    if (channelFile.isOpen()) channelFile.close();

    QFile recoveryFile(":/sabre/js/form_recovery.js");
    QString recoveryJs = recoveryFile.open(QFile::ReadOnly) ? QString(recoveryFile.readAll()) : QString();
    if (recoveryFile.isOpen()) recoveryFile.close();

    QWebEngineScript script;
    script.setName("sabre-form-recovery");
    script.setInjectionPoint(QWebEngineScript::DocumentReady);
    script.setWorldId(QWebEngineScript::MainWorld);
    script.setRunsOnSubFrames(true);
    script.setSourceCode(channelJs + "\n" + recoveryJs);
    m_view->page()->profile()->scripts()->insert(script);
}

void BrowserTab::setupFossilCache() {
    QFile channelFile(":/sabre/qwebchannel.js");
    QString channelJs = channelFile.open(QFile::ReadOnly) ? QString(channelFile.readAll()) : QString();
    if (channelFile.isOpen()) channelFile.close();

    QFile fossilFile(":/sabre/js/fossilize.js");
    QString fossilJs = fossilFile.open(QFile::ReadOnly) ? QString(fossilFile.readAll()) : QString();
    if (fossilFile.isOpen()) fossilFile.close();

    QWebEngineScript script;
    script.setName("sabre-fossilize");
    script.setInjectionPoint(QWebEngineScript::DocumentReady);
    script.setWorldId(QWebEngineScript::MainWorld);
    script.setRunsOnSubFrames(false);
    script.setSourceCode(channelJs + "\n" + fossilJs);
    m_view->page()->profile()->scripts()->insert(script);
}

void BrowserTab::injectHeuristic() {
    QFile channelFile(":/sabre/qwebchannel.js");
    QString channelJs = channelFile.open(QFile::ReadOnly) ? QString(channelFile.readAll()) : QString();
    if (channelFile.isOpen()) channelFile.close();

    QFile heuristicFile(":/sabre/js/fossil_heuristic.js");
    QString heuristicJs = heuristicFile.open(QFile::ReadOnly) ? QString(heuristicFile.readAll()) : QString();
    if (heuristicFile.isOpen()) heuristicFile.close();

    m_view->page()->runJavaScript(channelJs + "\n" + heuristicJs, QWebEngineScript::MainWorld);
}

void BrowserTab::navigateTo(const QString& url) {
    cancelRetry();
    if (url == "about://home" || url == "about://profile" || url == "about://settings" || url == "about://downloads" || url == "about://adblock") {
        m_currentLogicalUrl = url;
        emit urlChanged(url);
        m_urlBar->setText(url);

        if (url == "about://downloads") {
            if (!m_downloadsPage) {
                m_downloadsPage = new DownloadsPage(this);
                m_stack->addWidget(m_downloadsPage);
            }
            m_stack->setCurrentWidget(m_downloadsPage);
        } else {
            m_stack->setCurrentWidget(m_view);
        }
        return;
    }
    QString resolved = resolveInput(url);
    m_view->load(QUrl(resolved));
    m_urlBar->setText(resolved);
}

void BrowserTab::goBack() {
    cancelRetry();
    if (m_view->history()->canGoBack()) {
        m_view->back();
        m_stack->setCurrentWidget(m_view);
    }
}

void BrowserTab::goForward() {
    cancelRetry();
    if (m_view->history()->canGoForward()) {
        m_view->forward();
        m_stack->setCurrentWidget(m_view);
    }
}

void BrowserTab::reload()  { cancelRetry(); m_view->reload(); }
void BrowserTab::stop()    { cancelRetry(); m_view->stop(); }
QString BrowserTab::currentUrl()   const { return m_currentLogicalUrl; }
QString BrowserTab::currentTitle() const { return m_view->title(); }
bool    BrowserTab::canGoBack()    const { return m_view->history()->canGoBack(); }
bool    BrowserTab::canGoForward() const { return m_view->history()->canGoForward(); }

void BrowserTab::onNavigateRequested() {
    navigateTo(m_urlBar->text().trimmed());
}

void BrowserTab::onUrlChanged(const QUrl& url) {
    QString s = url.toString();
    if (s.isEmpty() || s == "about:blank" || s.startsWith("about:blank#")) return;
    m_currentLogicalUrl = s;
    m_urlBar->setText(s);

    bool https = s.startsWith("https://");
    m_secIcon->setText(https ? "●" : "○");
    m_secIcon->setStyleSheet(https ? "color:#2a8a2a; font-size:9px; padding:0 4px;" : "color:#555555; font-size:9px; padding:0 4px;");
    updateNavButtons();
    emit urlChanged(s);
}

void BrowserTab::onTitleChanged(const QString& title) {
    emit titleChanged(title);
}

void BrowserTab::onLoadStarted() {
    m_loading = true;
    m_reloadBtn->setIcon(QIcon(":/sabre/icons/close.svg"));
    m_progress->setValue(0);
    m_progress->show();
    emit loadingChanged(true);
}

void BrowserTab::onLoadFinished(bool ok) {
    m_loading = false;
    m_reloadBtn->setIcon(QIcon(":/sabre/icons/reload.svg"));
    m_progress->hide();
    m_progress->setValue(0);
    updateNavButtons();
    emit loadingChanged(false);
    if (ok) m_stack->setCurrentWidget(m_view);
}

void BrowserTab::onLoadProgress(int progress) {
    m_progress->setValue(progress);
    emit loadProgressChanged(progress);
}

void BrowserTab::updateNavButtons() {
    m_backBtn->setEnabled(m_view->history()->canGoBack());
    m_fwdBtn->setEnabled(m_view->history()->canGoForward());
}

QString BrowserTab::resolveInput(const QString& raw) {
    if (raw.isEmpty()) return {};
    if (raw.startsWith("about://")) return raw;
    if (raw.contains('.') && !raw.contains(' ')) return raw.startsWith("http") ? raw : "https://" + raw;
    QString contextId = m_fingerprint ? m_fingerprint->currentContextId() : "default";
    if (m_searchController) return m_searchController->buildQueryUrl(contextId, raw);
    return "https://duckduckgo.com/html/?q=" + QUrl::toPercentEncoding(raw);
}

bool BrowserTab::isRetryableError(const QWebEngineLoadingInfo& info) const {
    using S = QWebEngineLoadingInfo;
    if (info.errorDomain() == S::DnsErrorDomain) return false;
    if (info.errorDomain() == S::HttpErrorDomain) return (info.errorCode() == 502 || info.errorCode() == 503 || info.errorCode() == 504 || info.errorCode() == 429);
    if (info.errorDomain() == S::ConnectionErrorDomain) return true;
    return false;
}

void BrowserTab::scheduleRetry(const QUrl& url) {
    m_pendingRetryUrl = url;
    int shift = qMin(m_retryCount, 15);
    int delay = qMin(2000 * (1 << shift), 60000);
    m_retryCount++;
    m_urlBar->setText(QString("⏳ Retrying in %1s...").arg(delay / 1000));
    m_retryTimer->start(delay);
}

void BrowserTab::performRetry() {
    if (!m_pendingRetryUrl.isEmpty()) {
        m_urlBar->setText(m_pendingRetryUrl.toString());
        m_view->load(m_pendingRetryUrl);
        m_stack->setCurrentWidget(m_view);
    }
}

void BrowserTab::cancelRetry() {
    m_retryTimer->stop();
    m_retryCount = 0;
    m_pendingRetryUrl.clear();
}

void BrowserTab::onFossilizeRequested() {
    QUrl currentUrl = m_view->url();
    if (currentUrl.scheme() != "http" && currentUrl.scheme() != "https") return;
    m_currentFossilId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_fossilBridge->setCurrentFossil(m_currentFossilId, currentUrl);
    m_urlBar->setText("🦴 Fossilizing...");
    QString setFossilIdJs = QString("window.__currentFossilId = '%1';").arg(m_currentFossilId);
    m_view->page()->runJavaScript(setFossilIdJs, QWebEngineScript::MainWorld, [this](const QVariant&) {
        m_view->page()->runJavaScript("if(window.__sabreFossilizing) { window.__sabreFossilizing = false; }", QWebEngineScript::MainWorld);
        QFile f(QString(SOURCE_DIR) + "/assets/js/fossilize.js");
        if (f.open(QFile::ReadOnly)) { m_view->page()->runJavaScript(f.readAll(), QWebEngineScript::MainWorld); f.close(); }
    });
}

void BrowserTab::onAssetCollectionFinished() {
    QString fossilUrl = QString("sabre-fossil://%1").arg(m_currentFossilId);
    m_urlBar->setText(fossilUrl);
    m_view->load(QUrl(fossilUrl));
    m_leafIcon->setText("🦴");
    m_stack->setCurrentWidget(m_view);
}

void BrowserTab::onHeuristicReported(const QString& status) {
    if (status == "static") {
        m_leafIcon->setText("🌿");
        m_fossilBtn->setEnabled(true);
        m_fossilBtn->setToolTip("Save this page for offline reading (100% cacheable)");
    } else if (status == "dynamic") {
        m_leafIcon->setText("🍂");
        m_fossilBtn->setEnabled(true);
        m_fossilBtn->setToolTip("Save this page for offline reading (Text & images only)");
    } else if (status == "webapp") {
        m_leafIcon->setText("🍁");
        m_fossilBtn->setEnabled(false);
        m_fossilBtn->setToolTip("This web app cannot be saved for offline reading");
    } else {
        m_leafIcon->setText("");
        m_fossilBtn->setEnabled(false);
        m_fossilBtn->setToolTip("Save this page for offline reading");
    }
}

void BrowserTab::toggleDownloadPanel() {
    if (!m_downloadPanel) {
        m_downloadPanel = new DownloadPanel(this);
    }
    m_downloadPanel->showRelativeTo(m_downloadBtn);
}

bool BrowserTab::isPinned() const { return m_pinned; }

void BrowserTab::setPinned(bool pinned) {
    m_pinned = pinned;
}

bool BrowserTab::isMuted() const { return m_muted; }

void BrowserTab::setMuted(bool muted) {
    m_muted = muted;
    if (m_view && m_view->page()) {
        m_view->page()->setAudioMuted(muted);
    }
}

QWebEngineView* BrowserTab::webView() const { return m_view; }
#include "BrowserTab.moc"

// ═══════════════════════════════════════════ BrowserTab.cpp ═══════════════════════════════════════════
#include "BrowserTab.h"
#include "SearchController.h"
#include "FingerprintController.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineHistory>
#include <QUrl>

// ── Custom page — intercepts about://home, about://profile, about://settings ─
class SabrePage : public QWebEnginePage {
    Q_OBJECT
public:
    using QWebEnginePage::QWebEnginePage;

signals:
    void internalNavigation(const QString& url);

protected:
    bool acceptNavigationRequest(const QUrl& url, NavigationType, bool isMainFrame) override {
        if (!isMainFrame) return true;
        QString s = url.toString();
        if (url.scheme() == "about" &&
            (s == "about://home" || s == "about://profile" || s == "about://settings")) {
            emit internalNavigation(s);
            return false;
        }
        return QWebEnginePage::acceptNavigationRequest(url, NavigationType::NavigationTypeTyped, isMainFrame);
    }

    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel,
                                   const QString& msg, int line,
                                   const QString& src) override {
        qDebug().noquote() << "[JS]" << msg << "(" << src << ":" << line << ")";
    }
};

// ─────────────────────────────────────────────────────────────────────────────
BrowserTab::BrowserTab(QWebEngineProfile* profile, SearchController* searchController,
                       FingerprintController* fingerprint, QWidget* parent)
    : QWidget(parent), m_searchController(searchController), m_fingerprint(fingerprint)
{
    setupUI();
    setupProfile(profile);
}

void BrowserTab::setupUI() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Toolbar ───────────────────────────────────────────────────────────────
    auto* toolbar = new QWidget(this);
    toolbar->setFixedHeight(46);
    toolbar->setStyleSheet("background:#111111; border-bottom:1px solid #1c1c1c;");

    auto* tl = new QHBoxLayout(toolbar);
    tl->setContentsMargins(8, 6, 8, 6);
    tl->setSpacing(4);

    auto makeBtn = [&](const QString& iconPath) {
        auto* btn = new QPushButton(toolbar);
        btn->setIcon(QIcon(QString(SOURCE_DIR) + "/assets/icons/" + iconPath));
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

    m_backBtn   = makeBtn("back.svg");
    m_fwdBtn    = makeBtn("forward.svg");
    m_reloadBtn = makeBtn("reload.svg");
    m_backBtn->setEnabled(false);
    m_fwdBtn->setEnabled(false);

    m_secIcon = new QLabel("◈", toolbar);
    m_secIcon->setStyleSheet("color:#ff2d2d; font-size:9px; padding:0 4px;");

    m_urlBar = new QLineEdit(toolbar);
    m_urlBar->setPlaceholderText("search or enter address");
    m_urlBar->setStyleSheet(R"(
        QLineEdit {
            background: #161616;
            color: #cccccc;
            border: 1px solid #262626;
            border-radius: 2px;
            padding: 4px 10px;
            font-family: "GeistMono", monospace;
            font-size: 11px;
        }
        QLineEdit:focus {
            border-color: #3a3a3a;
            color: #ffffff;
        }
    )");

    // ── Progress bar (1px, sits at bottom of toolbar) ─────────────────────────
    m_progress = new QProgressBar(this);
    m_progress->setFixedHeight(1);
    m_progress->setTextVisible(false);
    m_progress->setStyleSheet(
        "QProgressBar { background:transparent; border:none; }"
        "QProgressBar::chunk { background:#ff2d2d; }"
    );
    m_progress->hide();

    tl->addWidget(m_backBtn);
    tl->addWidget(m_fwdBtn);
    tl->addWidget(m_reloadBtn);
    tl->addWidget(m_secIcon);
    tl->addWidget(m_urlBar, 1);

    m_view = new QWebEngineView(this);
    m_view->setStyleSheet("background:#0e0e0e;");

    root->addWidget(toolbar);
    root->addWidget(m_progress);
    root->addWidget(m_view, 1);

    // ── Connections ───────────────────────────────────────────────────────────
    connect(m_backBtn,   &QPushButton::clicked, this, &BrowserTab::goBack);
    connect(m_fwdBtn,    &QPushButton::clicked, this, &BrowserTab::goForward);
    connect(m_reloadBtn, &QPushButton::clicked, this, [this] {
        m_loading ? m_view->stop() : m_view->reload();
    });
    connect(m_urlBar, &QLineEdit::returnPressed, this, &BrowserTab::onNavigateRequested);
}

void BrowserTab::setupProfile(QWebEngineProfile* profile) {
    auto* page = new SabrePage(profile, m_view);
    m_view->setPage(page);

    connect(page, &SabrePage::internalNavigation, this, [this](const QString& url) {
        m_currentLogicalUrl = url;
        emit urlChanged(url);
    });

    connect(m_view, &QWebEngineView::urlChanged,   this, &BrowserTab::onUrlChanged);
    connect(m_view, &QWebEngineView::titleChanged,  this, &BrowserTab::onTitleChanged);
    connect(m_view, &QWebEngineView::loadStarted,   this, &BrowserTab::onLoadStarted);
    connect(m_view, &QWebEngineView::loadFinished,  this, &BrowserTab::onLoadFinished);
    connect(m_view, &QWebEngineView::loadProgress,  this, &BrowserTab::onLoadProgress);

    auto* s = m_view->settings();
    s->setAttribute(QWebEngineSettings::JavascriptEnabled,          true);
    s->setAttribute(QWebEngineSettings::WebGLEnabled,               true);
    s->setAttribute(QWebEngineSettings::Accelerated2dCanvasEnabled, true);
    s->setAttribute(QWebEngineSettings::AutoLoadImages,             true);
    s->setAttribute(QWebEngineSettings::FullScreenSupportEnabled,   false);
}

// ── Public API ────────────────────────────────────────────────────────────────
void BrowserTab::navigateTo(const QString& url) {
    if (url == "about://home" || url == "about://profile" || url == "about://settings") {
        m_currentLogicalUrl = url;
        emit urlChanged(url);
        m_urlBar->setText(url);
        return;
    }
    QString resolved = resolveInput(url);
    m_view->load(QUrl(resolved));
    m_urlBar->setText(resolved);
}

void BrowserTab::goBack() {
    if (m_view->history()->canGoBack())
        m_view->back();
}

void BrowserTab::goForward() {
    if (m_view->history()->canGoForward())
        m_view->forward();
}

void BrowserTab::reload()  { m_view->reload(); }
void BrowserTab::stop()    { m_view->stop(); }

QString BrowserTab::currentUrl()   const { return m_currentLogicalUrl; }
QString BrowserTab::currentTitle() const { return m_view->title(); }
bool    BrowserTab::canGoBack()    const { return m_view->history()->canGoBack(); }
bool    BrowserTab::canGoForward() const { return m_view->history()->canGoForward(); }

// ── Private slots ─────────────────────────────────────────────────────────────
void BrowserTab::onNavigateRequested() {
    QString url = resolveInput(m_urlBar->text().trimmed());
    if (url == "about://home" || url == "about://profile" || url == "about://settings") {
        navigateTo(url);
        return;
    }
    m_view->load(QUrl(url));
    m_urlBar->setText(url);
}

void BrowserTab::onUrlChanged(const QUrl& url) {
    QString s = url.toString();

    // FLICKER FIX: QWebEngine emits urlChanged("") and urlChanged("about:blank")
    // as transient states during load startup and between navigations.
    // Propagating those causes NormalWindow to switch the page stack back and
    // forth for one frame — the visible flicker. We simply skip them; the engine
    // always follows up with the real destination URL.
    if (s.isEmpty() || s == "about:blank")
        return;

    m_currentLogicalUrl = s;
    m_urlBar->setText(s);

    bool https = s.startsWith("https://");
    m_secIcon->setText(https ? "●" : "○");
    m_secIcon->setStyleSheet(
        https ? "color:#2a8a2a; font-size:9px; padding:0 4px;"
              : "color:#555555; font-size:9px; padding:0 4px;"
    );
    updateNavButtons();
    emit urlChanged(s);
}

void BrowserTab::onTitleChanged(const QString& title) {
    emit titleChanged(title);
}

void BrowserTab::onLoadStarted() {
    m_loading = true;
    m_reloadBtn->setIcon(QIcon(QString(SOURCE_DIR) + "/assets/icons/close.svg"));
    m_progress->setValue(0);
    m_progress->show();
    emit loadingChanged(true);
}

void BrowserTab::onLoadFinished(bool) {
    m_loading = false;
    m_reloadBtn->setIcon(QIcon(QString(SOURCE_DIR) + "/assets/icons/reload.svg"));
    m_progress->hide();
    m_progress->setValue(0);
    updateNavButtons();
    emit loadingChanged(false);
}

void BrowserTab::onLoadProgress(int progress) {
    m_progress->setValue(progress);
    emit loadProgressChanged(progress);
}

void BrowserTab::updateNavButtons() {
    m_backBtn->setEnabled(m_view->history()->canGoBack());
    m_fwdBtn->setEnabled(m_view->history()->canGoForward());
}

// ── URL resolution ────────────────────────────────────────────────────────────
QString BrowserTab::resolveInput(const QString& raw) {
    if (raw.isEmpty()) return {};

    if (raw.startsWith("about://"))
        return raw;

    if (raw.contains('.') && !raw.contains(' '))
        return raw.startsWith("http") ? raw : "https://" + raw;

    QString contextId = m_fingerprint ? m_fingerprint->currentContextId() : "default";
    if (m_searchController)
        return m_searchController->buildQueryUrl(contextId, raw);

    return "https://duckduckgo.com/html/?q=" + QUrl::toPercentEncoding(raw);
}

#include "BrowserTab.moc"

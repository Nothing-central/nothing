#include "SettingsPage.h"
#include "FingerprintController.h"
#include "LangManager.h"
#include "SearchController.h"
#include <QtWebEngineCore/QWebEngineProfile>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QComboBox>
#include <QFileDialog>
#include <QSettings>
#include <QScrollArea>

static const QString kMono = R"(font-family:"GeistMono",monospace;)";
static const QString kBg   = "background:#0e0e0e;";

static QSettings appSettings() {
    return QSettings("Ernest Tech House", "Sabre Browser");
}

SettingsPage::SettingsPage(FingerprintController* fp, QWebEngineProfile* webProfile,
                           LangManager* lang, SearchController* searchController, QWidget* parent)
    : QWidget(parent), m_fp(fp), m_webProfile(webProfile), m_lang(lang), m_searchController(searchController)
{
    setStyleSheet("QWidget { " + kBg + " }");
    setAttribute(Qt::WA_StyledBackground, true);
    buildUI();

    if (m_lang)
        connect(m_lang, &LangManager::langChanged, this, &SettingsPage::onLangChanged);
}

void SettingsPage::onLangChanged() {
    if (!layout()) return;
    QLayoutItem* item;
    while ((item = layout()->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    delete layout();
    buildUI();
}

void SettingsPage::buildUI() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setStyleSheet("QScrollArea { background:#0e0e0e; border:none; }");

    auto* content = new QWidget;
    content->setStyleSheet("background:#0e0e0e;");

    auto* root = new QVBoxLayout(content);
    root->setContentsMargins(40, 32, 40, 40);
    root->setSpacing(28);

    // ── Title ─────────────────────────────────────────────────────────────────
    auto* title = new QLabel("SETTINGS", content);
    title->setStyleSheet("color:#ffffff; " + kMono + " font-size:24px; letter-spacing:2px; background:transparent;");
    root->addWidget(title);

    auto* div = new QFrame(content);
    div->setFrameShape(QFrame::HLine);
    div->setStyleSheet("background:#1a1a1a; border:none; max-height:1px;");
    root->addWidget(div);

    // ── Identity / Profile ────────────────────────────────────────────────────
    auto* sectionLbl = new QLabel("IDENTITY / PROFILE", content);
    sectionLbl->setStyleSheet("color:#666666; " + kMono + " font-size:12px; letter-spacing:3px; background:transparent;");
    root->addWidget(sectionLbl);

    auto* box = new QWidget(content);
    box->setStyleSheet("background:#141414; border:1px solid #202020; border-radius:4px;");
    box->setAttribute(Qt::WA_StyledBackground, true);
    auto* boxLayout = new QVBoxLayout(box);
    boxLayout->setContentsMargins(20, 20, 20, 20);
    boxLayout->setSpacing(14);

    QString currentCtx = m_fp ? m_fp->currentContextId() : "default";
    m_currentContextLabel = new QLabel("Current context: " + currentCtx, box);
    m_currentContextLabel->setStyleSheet("color:#aaaaaa; " + kMono + " font-size:13px; background:transparent;");
    m_currentContextLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    boxLayout->addWidget(m_currentContextLabel);

    auto* desc = new QLabel("Generates a brand new fingerprint identity and applies it to the active browser profile immediately.", box);
    desc->setWordWrap(true);
    desc->setStyleSheet("color:#555555; " + kMono + " font-size:12px; background:transparent;");
    boxLayout->addWidget(desc);

    auto* changeBtn = new QPushButton("CHANGE PROFILE", box);
    changeBtn->setFixedSize(170, 36);
    changeBtn->setCursor(Qt::PointingHandCursor);
    changeBtn->setStyleSheet(R"(
        QPushButton {
            background:#1c1c1c; color:#eeeeee;
            border:1px solid #2a2a2a; border-radius:3px;
            font-family:"GeistMono",monospace; font-size:11px; letter-spacing:2px;
        }
        QPushButton:hover { background:#242424; }
        QPushButton:pressed { background:#161616; }
    )");
    connect(changeBtn, &QPushButton::clicked, this, &SettingsPage::onChangeProfileClicked);
    boxLayout->addWidget(changeBtn, 0, Qt::AlignLeft);
    root->addWidget(box);

    // ── Language ──────────────────────────────────────────────────────────────
    auto* langSectionLbl = new QLabel("LANGUAGE", content);
    langSectionLbl->setStyleSheet("color:#666666; " + kMono + " font-size:12px; letter-spacing:3px; background:transparent;");
    root->addWidget(langSectionLbl);

    auto* langBox = new QWidget(content);
    langBox->setStyleSheet("background:#141414; border:1px solid #202020; border-radius:4px;");
    langBox->setAttribute(Qt::WA_StyledBackground, true);
    auto* langBoxLayout = new QVBoxLayout(langBox);
    langBoxLayout->setContentsMargins(20, 20, 20, 20);
    langBoxLayout->setSpacing(14);

    auto* langDesc = new QLabel("Choose the display language for the browser interface.", langBox);
    langDesc->setWordWrap(true);
    langDesc->setStyleSheet("color:#555555; " + kMono + " font-size:12px; background:transparent;");
    langBoxLayout->addWidget(langDesc);

    m_langCombo = new QComboBox(langBox);
    m_langCombo->addItem("English",  "en");
    m_langCombo->addItem("한국어",    "ko");
    m_langCombo->addItem("Kiswahili","sw");
    m_langCombo->addItem("Tagalog",  "tl");
    m_langCombo->addItem("中文",      "zh");
    if (m_lang) {
        int idx = m_langCombo->findData(m_lang->lang());
        if (idx >= 0) m_langCombo->setCurrentIndex(idx);
    }
    m_langCombo->setFixedWidth(200);
    m_langCombo->setStyleSheet(R"(
        QComboBox {
            background:#1c1c1c; color:#eeeeee;
            border:1px solid #2a2a2a; border-radius:3px;
            padding:6px 10px;
            font-family:"GeistMono",monospace; font-size:12px;
        }
        QComboBox:hover { background:#242424; }
        QComboBox QAbstractItemView {
            background:#1c1c1c; color:#eeeeee;
            border:1px solid #2a2a2a;
            selection-background-color:#2a2a2a;
        }
    )");
    connect(m_langCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPage::onLanguageSelected);
    langBoxLayout->addWidget(m_langCombo, 0, Qt::AlignLeft);
    root->addWidget(langBox);

    // ── Search Engine ─────────────────────────────────────────────────────────
    auto* engineSectionLbl = new QLabel("SEARCH ENGINE", content);
    engineSectionLbl->setStyleSheet("color:#666666; " + kMono + " font-size:12px; letter-spacing:3px; background:transparent;");
    root->addWidget(engineSectionLbl);

    auto* engineBox = new QWidget(content);
    engineBox->setStyleSheet("background:#141414; border:1px solid #202020; border-radius:4px;");
    engineBox->setAttribute(Qt::WA_StyledBackground, true);
    auto* engineBoxLayout = new QVBoxLayout(engineBox);
    engineBoxLayout->setContentsMargins(20, 20, 20, 20);
    engineBoxLayout->setSpacing(14);

    m_engineCombo = new QComboBox(engineBox);
    if (m_searchController) {
        QString contextId = m_fp ? m_fp->currentContextId() : "default";
        for (const QString& id : m_searchController->engineIds())
            m_engineCombo->addItem(m_searchController->engineDisplayName(id), id);
        int idx = m_engineCombo->findData(m_searchController->currentEngineId(contextId));
        if (idx >= 0) m_engineCombo->setCurrentIndex(idx);
    }
    m_engineCombo->setFixedWidth(220);
    m_engineCombo->setStyleSheet(R"(
        QComboBox {
            background:#1c1c1c; color:#eeeeee;
            border:1px solid #2a2a2a; border-radius:3px;
            padding:6px 10px;
            font-family:"GeistMono",monospace; font-size:12px;
        }
        QComboBox:hover { background:#242424; }
        QComboBox QAbstractItemView {
            background:#1c1c1c; color:#eeeeee;
            border:1px solid #2a2a2a;
            selection-background-color:#2a2a2a;
        }
    )");
    connect(m_engineCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPage::onSearchEngineSelected);
    engineBoxLayout->addWidget(m_engineCombo, 0, Qt::AlignLeft);
    root->addWidget(engineBox);

    // ── Background ────────────────────────────────────────────────────────────
    auto* bgSectionLbl = new QLabel("NEW TAB BACKGROUND", content);
    bgSectionLbl->setStyleSheet("color:#666666; " + kMono + " font-size:12px; letter-spacing:3px; background:transparent;");
    root->addWidget(bgSectionLbl);

    auto* bgBox = new QWidget(content);
    bgBox->setStyleSheet("background:#141414; border:1px solid #202020; border-radius:4px;");
    bgBox->setAttribute(Qt::WA_StyledBackground, true);
    auto* bgBoxLayout = new QVBoxLayout(bgBox);
    bgBoxLayout->setContentsMargins(20, 20, 20, 20);
    bgBoxLayout->setSpacing(14);

    auto* bgDesc = new QLabel("Choose how the new tab page background looks.", bgBox);
    bgDesc->setWordWrap(true);
    bgDesc->setStyleSheet("color:#555555; " + kMono + " font-size:12px; background:transparent;");
    bgBoxLayout->addWidget(bgDesc);

    m_bgCombo = new QComboBox(bgBox);
    m_bgCombo->addItem("Default Image",  0);  // bundled elysiamain.jpeg
    m_bgCombo->addItem("Custom Image",   1);  // user picked path
    m_bgCombo->addItem("Solid Dark",     2);  // #0a0a0a
    m_bgCombo->addItem("Gradient",       3);  // dark gradient
    m_bgCombo->setFixedWidth(220);
    m_bgCombo->setStyleSheet(R"(
        QComboBox {
            background:#1c1c1c; color:#eeeeee;
            border:1px solid #2a2a2a; border-radius:3px;
            padding:6px 10px;
            font-family:"GeistMono",monospace; font-size:12px;
        }
        QComboBox:hover { background:#242424; }
        QComboBox QAbstractItemView {
            background:#1c1c1c; color:#eeeeee;
            border:1px solid #2a2a2a;
            selection-background-color:#2a2a2a;
        }
    )");

    // restore saved mode
    QSettings s = appSettings();
    int savedMode = s.value("newtab/bgMode", 0).toInt();
    m_bgCombo->setCurrentIndex(savedMode);

    connect(m_bgCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPage::onBgModeSelected);
    bgBoxLayout->addWidget(m_bgCombo, 0, Qt::AlignLeft);

    // custom image row
    auto* customRow = new QHBoxLayout;
    customRow->setSpacing(10);

    m_pickImageBtn = new QPushButton("PICK IMAGE", bgBox);
    m_pickImageBtn->setFixedSize(120, 32);
    m_pickImageBtn->setCursor(Qt::PointingHandCursor);
    m_pickImageBtn->setEnabled(savedMode == 1);
    m_pickImageBtn->setStyleSheet(R"(
        QPushButton {
            background:#1c1c1c; color:#eeeeee;
            border:1px solid #2a2a2a; border-radius:3px;
            font-family:"GeistMono",monospace; font-size:11px; letter-spacing:2px;
        }
        QPushButton:hover { background:#242424; }
        QPushButton:pressed { background:#161616; }
        QPushButton:disabled { color:#444444; border-color:#1a1a1a; }
    )");
    connect(m_pickImageBtn, &QPushButton::clicked, this, &SettingsPage::onPickCustomImage);

    QString savedPath = s.value("newtab/bgCustomPath", "").toString();
    m_customPathLabel = new QLabel(savedPath.isEmpty() ? "no image selected" : savedPath, bgBox);
    m_customPathLabel->setStyleSheet("color:#444444; " + kMono + " font-size:10px; background:transparent;");
    m_customPathLabel->setVisible(savedMode == 1);

    customRow->addWidget(m_pickImageBtn);
    customRow->addWidget(m_customPathLabel, 1);
    bgBoxLayout->addLayout(customRow);

    root->addWidget(bgBox);
    root->addStretch();

    m_scrollArea->setWidget(content);
    outer->addWidget(m_scrollArea);
}

void SettingsPage::onChangeProfileClicked() {
    if (!m_fp) return;
    QString newId = m_fp->newProfile();
    m_fp->applyToWebProfile(m_webProfile);
    m_currentContextLabel->setText("Current context: " + newId);
    emit profileChanged(newId);
}

void SettingsPage::onLanguageSelected(int index) {
    if (!m_lang || index < 0) return;
    QString code = m_langCombo->itemData(index).toString();
    if (code != m_lang->lang())
        m_lang->setLang(code);
}

void SettingsPage::onSearchEngineSelected(int index) {
    if (!m_searchController || index < 0) return;
    QString id = m_engineCombo->itemData(index).toString();
    QString contextId = m_fp ? m_fp->currentContextId() : "default";
    m_searchController->setEngine(contextId, id);
}

void SettingsPage::onBgModeSelected(int index) {
    QSettings s = appSettings();
    s.setValue("newtab/bgMode", index);
    m_pickImageBtn->setEnabled(index == 1);
    m_customPathLabel->setVisible(index == 1);
    QString customPath = s.value("newtab/bgCustomPath", "").toString();
    emit backgroundModeChanged(index, customPath);
}

void SettingsPage::onPickCustomImage() {
    QString path = QFileDialog::getOpenFileName(
        this, "Select Background Image", "",
        "Images (*.png *.jpg *.jpeg *.webp *.bmp)"
    );
    if (path.isEmpty()) return;
    QSettings s = appSettings();
    s.setValue("newtab/bgCustomPath", path);
    m_customPathLabel->setText(path);
    emit backgroundModeChanged(1, path);
}

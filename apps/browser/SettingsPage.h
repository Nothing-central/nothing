#pragma once
#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QScrollArea>

QT_BEGIN_NAMESPACE
class QWebEngineProfile;
class QLabel;
QT_END_NAMESPACE
class FingerprintController;
class LangManager;
class SearchController;

class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(FingerprintController* fp, QWebEngineProfile* webProfile,
                          LangManager* lang, SearchController* searchController,
                          QWidget* parent = nullptr);
signals:
    void profileChanged(const QString& contextId);
    void backgroundModeChanged(int mode, const QString& customPath);
private slots:
    void onChangeProfileClicked();
    void onLangChanged();
    void onLanguageSelected(int index);
    void onSearchEngineSelected(int index);
    void onBgModeSelected(int index);
    void onPickCustomImage();
private:
    void buildUI();
    FingerprintController* m_fp             = nullptr;
    QWebEngineProfile*     m_webProfile     = nullptr;
    LangManager*           m_lang           = nullptr;
    SearchController*      m_searchController = nullptr;
    QLabel*    m_currentContextLabel = nullptr;
    QComboBox* m_langCombo           = nullptr;
    QComboBox* m_engineCombo         = nullptr;
    QComboBox* m_bgCombo             = nullptr;
    QPushButton* m_pickImageBtn      = nullptr;
    QLabel*    m_customPathLabel     = nullptr;
    QScrollArea* m_scrollArea        = nullptr;
};

// ═══════════════════════════════════════════ NewTabPage.h ═════════════════════════════════════════════
#pragma once
#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include <QPixmap>
class SearchController;

class NewTabPage : public QWidget {
    Q_OBJECT
public:
    explicit NewTabPage(SearchController* searchController = nullptr,
                        QWidget* parent = nullptr);
signals:
    void navigateRequested(const QString& url);
    void showProfile();
    void showSettings();
public slots:
    void refreshEngineLabel();
    void setBackgroundMode(int mode, const QString& customPath);
private slots:
    void onTick();
    void onSearch();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
private:
    void buildUI();
    QString resolveInput(const QString& raw);
    SearchController* m_searchController = nullptr;
    QLabel*    m_clock        = nullptr;
    QLabel*    m_date         = nullptr;
    QLabel*    m_tz           = nullptr;
    QLabel*    m_engineLabel  = nullptr;
    QLineEdit* m_search       = nullptr;
    QTimer*    m_timer        = nullptr;
    QPixmap    m_bgPixmap;
    int     m_bgMode       = 0;
    QString m_bgCustomPath;
};

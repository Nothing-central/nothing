// ═══════════════════════════════════════════ NoInternetPage.h ════════════════════════════════════════
#pragma once
#include <QWidget>
#include <QLabel>
#include <QPushButton>

class NoInternetPage : public QWidget {
    Q_OBJECT
public:
    explicit NoInternetPage(QWidget* parent = nullptr);

signals:
    void retryRequested();
    void playOfflineGame();

private:
    void buildUI();
    QLabel*      m_image       = nullptr;
    QLabel*      m_title       = nullptr;
    QLabel*      m_subtitle    = nullptr;
    QPushButton* m_retryBtn    = nullptr;
    QPushButton* m_gameBtn     = nullptr;
};

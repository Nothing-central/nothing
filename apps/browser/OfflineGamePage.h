// ═══════════════════════════════════════════ OfflineGamePage.h ═══════════════════════════════════════
#pragma once
#include <QWidget>

class OfflineGamePage : public QWidget {
    Q_OBJECT
public:
    explicit OfflineGamePage(QWidget* parent = nullptr);

signals:
    void backRequested();   // user wants to go back to the no-internet page

private:
    void buildUI();
};

// ═══════════════════════════════════════════ SabreTabBar.h ════════════════════════════════════════════
#pragma once
#include <QTabBar>
#include <QMenu>
#include <QAction>
#include <QContextMenuEvent>
#include <QVariantMap>

class SabreTabBar : public QTabBar {
    Q_OBJECT
public:
    explicit SabreTabBar(QWidget* parent = nullptr);

signals:
    void requestPin(int index);
    void requestUnpin(int index);
    void requestDuplicate(int index);
    void requestNewTabRight(int index);
    void requestNewTabLeft(int index);
    void requestMute(int index);
    void requestUnmute(int index);
    void requestReload(int index);
    void requestSplitView(int index);
    void requestClose(int index);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void buildMenu(int tabIndex);
    int m_contextTabIndex = -1;
};

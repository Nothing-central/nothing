// ═══════════════════════════════════════════ SabreTabBar.cpp ══════════════════════════════════════════
#include "SabreTabBar.h"
#include <QStyle>
#include <QCursor>

SabreTabBar::SabreTabBar(QWidget* parent) : QTabBar(parent) {
    setExpanding(false);
    setMovable(true);
    setTabsClosable(true);
    setUsesScrollButtons(true);
    setDocumentMode(true);

    // Inline SVG for a crisp, always-visible close button (no external files needed)
    QString closeSvg = "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path fill='%23888888' d='M4.646 4.646a.5.5 0 0 1 .708 0L8 7.293l2.646-2.647a.5.5 0 0 1 .708.708L8.707 8l2.647 2.646a.5.5 0 0 1-.708.708L8 8.707l-2.646 2.647a.5.5 0 0 1-.708-.708L7.293 8 4.646 5.354a.5.5 0 0 1 0-.708z'/></svg>";
    QString closeSvgHover = "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path fill='%23ffffff' d='M4.646 4.646a.5.5 0 0 1 .708 0L8 7.293l2.646-2.647a.5.5 0 0 1 .708.708L8.707 8l2.647 2.646a.5.5 0 0 1-.708.708L8 8.707l-2.646 2.647a.5.5 0 0 1-.708-.708L7.293 8 4.646 5.354a.5.5 0 0 1 0-.708z'/></svg>";

    setStyleSheet(QString(R"(
         QTabBar {
             background: #0c0c0c;
             border-bottom: 1px solid #1a1a1a;
             padding: 4px 4px 0 4px;
         }
         QTabBar::tab {
             background: transparent;
             color: #888888;
             border: 1px solid transparent;
             border-bottom: none;
             border-top-left-radius: 6px;
             border-top-right-radius: 6px;
             padding: 6px 28px 6px 12px; /* Extra right padding for close button */
             min-width: 100px;
             max-width: 200px;
             font-family: "GeistMono", monospace;
             font-size: 12px;
             margin-right: 2px;
         }
         QTabBar::tab:selected  {
             background: #161616;
             color: #ffffff;
             border: 1px solid #2a2a2a;
             border-bottom: 1px solid #161616;
             margin-bottom: -1px;
         }
         QTabBar::tab:hover:!selected {
             background: #111111;
             color: #cccccc;
         }
         QTabBar::tab:pinned {
             min-width: 36px;
             max-width: 36px;
             padding: 4px 10px;
         }
         QTabBar::close-button {
             subcontrol-position: right;
             subcontrol-origin: padding;
             width: 16px;
             height: 16px;
             image: url(%1);
             background: transparent;
             border: none;
             border-radius: 4px;
             padding: 2px;
         }
         QTabBar::close-button:hover   { background: #ff2d2d; image: url(%2); }
         QTabBar::close-button:pressed { background: #cc2424; }
     )").arg(closeSvg, closeSvgHover));

    connect(this, &QTabBar::tabCloseRequested, this, &SabreTabBar::requestClose);
}

void SabreTabBar::contextMenuEvent(QContextMenuEvent* event) {
    int tabIndex = tabAt(event->pos());
    if (tabIndex < 0) return;
    m_contextTabIndex = tabIndex;
    buildMenu(tabIndex);
}

void SabreTabBar::buildMenu(int tabIndex) {
    QMenu menu(this);
    menu.setStyleSheet(R"(
        QMenu {
            background: #161616;
            color: #cccccc;
            border: 1px solid #262626;
            border-radius: 6px;
            padding: 4px 0px;
            font-size: 12px;
            font-family: "GeistMono", monospace;
        }
        QMenu::item {
            padding: 6px 24px;
            border-radius: 4px;
            margin: 1px 4px;
        }
        QMenu::item:selected {
            background: #1e1e1e;
            color: #ffffff;
        }
        QMenu::separator {
            height: 1px;
            background: #262626;
            margin: 4px 8px;
        }
    )");

    QVariantMap data = tabData(tabIndex).toMap();
    bool pinned = data.value("pinned", false).toBool();
    bool muted  = data.value("muted", false).toBool();

    if (pinned) {
        QAction* unpinAction = menu.addAction("📌  Unpin Tab");
        connect(unpinAction, &QAction::triggered, this, [this]() { emit requestUnpin(m_contextTabIndex); });
    } else {
        QAction* pinAction = menu.addAction("📌  Pin Tab");
        connect(pinAction, &QAction::triggered, this, [this]() { emit requestPin(m_contextTabIndex); });
    }

    menu.addSeparator();
    QAction* reloadAction = menu.addAction("🔄  Reload");
    connect(reloadAction, &QAction::triggered, this, [this]() { emit requestReload(m_contextTabIndex); });

    QAction* dupAction = menu.addAction("📋  Duplicate Tab");
    connect(dupAction, &QAction::triggered, this, [this]() { emit requestDuplicate(m_contextTabIndex); });

    menu.addSeparator();
    QAction* leftAction = menu.addAction("⬅  New Tab to the Left");
    connect(leftAction, &QAction::triggered, this, [this]() { emit requestNewTabLeft(m_contextTabIndex); });

    QAction* rightAction = menu.addAction("➡  New Tab to the Right");
    connect(rightAction, &QAction::triggered, this, [this]() { emit requestNewTabRight(m_contextTabIndex); });

    menu.addSeparator();
    if (muted) {
        QAction* unmuteAction = menu.addAction("🔊  Unmute Tab");
        connect(unmuteAction, &QAction::triggered, this, [this]() { emit requestUnmute(m_contextTabIndex); });
    } else {
        QAction* muteAction = menu.addAction("🔇  Mute Tab");
        connect(muteAction, &QAction::triggered, this, [this]() { emit requestMute(m_contextTabIndex); });
    }

    menu.addSeparator();
    QAction* splitAction = menu.addAction("📐  Create Split View");
    connect(splitAction, &QAction::triggered, this, [this]() { emit requestSplitView(m_contextTabIndex); });

    menu.exec(QCursor::pos());
}

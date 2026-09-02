#include "DownloadsPage.h"
#include "DownloadPanel.h" // Reuses DownloadItemWidget
#include <QLabel>
#include <QHBoxLayout>

DownloadsPage::DownloadsPage(QWidget* parent) : QWidget(parent) {
    setStyleSheet("background:#0e0e0e;");
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(40, 40, 40, 40);
    rootLayout->setSpacing(20);

    auto* header = new QLabel("🦴 Download History", this);
    header->setStyleSheet("color:#ffffff; font-size:24px; font-weight:bold;");

    auto* clearBtn = new QPushButton("Clear All History", this);
    clearBtn->setFixedSize(140, 36);
    clearBtn->setStyleSheet("QPushButton{background:#ff2d2d; color:#ffffff; border:none; border-radius:4px; font-weight:bold;} QPushButton:hover{background:#ff4d4d;}");
    connect(clearBtn, &QPushButton::clicked, this, []() {
        DownloadManager::instance()->clearHistory();
    });

    auto* topLayout = new QHBoxLayout();
    topLayout->addWidget(header);
    topLayout->addStretch();
    topLayout->addWidget(clearBtn);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea{border:none; background:transparent;}");

    auto* listContainer = new QWidget();
    listContainer->setStyleSheet("background:transparent;");
    auto* listLayout = new QVBoxLayout(listContainer);
    listLayout->setContentsMargins(0,0,0,0);
    listLayout->setSpacing(8);
    listLayout->addStretch();

    auto records = DownloadManager::instance()->getHistory();
    if (records.isEmpty()) {
        auto* emptyLabel = new QLabel("No downloads yet.", listContainer);
        emptyLabel->setStyleSheet("color:#666666; font-size:14px; padding:20px;");
        listLayout->insertWidget(0, emptyLabel);
    } else {
        for (const auto& rec : records) {
            auto* item = new DownloadItemWidget(rec, listContainer);
            item->setFixedHeight(70); // Slightly taller for full page
            if (rec.status == "completed") item->setFinished(true);
            else if (rec.status == "failed") item->setFinished(false);
            listLayout->insertWidget(listLayout->count() - 1, item);
        }
    }

    scrollArea->setWidget(listContainer);
    rootLayout->addLayout(topLayout);
    rootLayout->addWidget(scrollArea, 1);
}

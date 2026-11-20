#include "ui/StatusIcon.hpp"

#include <QFont>
#include <QPainter>
#include <QPixmap>

namespace controld {

QIcon StatusIcon::create(RuntimeStatus status) {
    QColor color(127, 140, 141);
    switch (status) {
    case RuntimeStatus::Active:
        color = QColor(31, 138, 76);
        break;
    case RuntimeStatus::Failed:
        color = QColor(192, 57, 43);
        break;
    case RuntimeStatus::Disabled:
        color = QColor(127, 140, 141);
        break;
    }

    QIcon icon;
    const int sizes[] = {16, 22, 24, 32, 48};
    for (const int size : sizes) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        const qreal margin = size * 0.08;
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QRectF(margin, margin, size - (2 * margin), size - (2 * margin)));
        painter.setPen(Qt::white);
        QFont font = painter.font();
        font.setBold(true);
        font.setPixelSize(qMax(1, int(size * 0.62)));
        painter.setFont(font);
        painter.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral("C"));
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

} // namespace controld

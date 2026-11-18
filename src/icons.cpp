#include "icons.h"

QIcon Icons::named(const QString &name, const QString &fallback)
{
    return QIcon::fromTheme(name, QIcon(QString(":/images/icons/%1.svg").arg(fallback.isEmpty() ? name : fallback)));
}

QIcon Icons::bundled(const QString &name)
{
    return QIcon(QString(":/images/icons/%1.svg").arg(name));
}

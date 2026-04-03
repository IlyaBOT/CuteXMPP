#pragma once

#include "src/models/types.h"

#include <QString>

namespace CuteXmpp {

QList<ThemePalette> availableThemes();
ThemePalette themeById(const QString& id);
QString buildApplicationStyleSheet(const ThemePalette& theme);
QString formattedTimestamp(const QDateTime& timestamp);
QString presenceText(PresenceState state);
QString initialsForName(const QString& value);

}  // namespace CuteXmpp

#pragma once

#include <QDir>
#include <QSettings>

namespace CuteXmpp {

QString appDataPath();
QDir ensureAppDataDir();
QSettings createSettings();

}  // namespace CuteXmpp

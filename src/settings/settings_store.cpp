#include "src/settings/settings_store.h"

#include <QDir>
#include <QStandardPaths>

namespace CuteXmpp {

QString appDataPath()
{
#ifdef Q_OS_WIN
    const QString roamingAppData = qEnvironmentVariable("APPDATA");
    if (!roamingAppData.isEmpty()) {
        return QDir(roamingAppData).filePath("CuteXMPP");
    }
#endif
    const QString basePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(basePath).filePath("CuteXMPP");
}

QDir ensureAppDataDir()
{
    QDir dir(appDataPath());
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    return dir;
}

QSettings createSettings()
{
    const QDir dir = ensureAppDataDir();
    return QSettings(dir.filePath("settings.ini"), QSettings::IniFormat);
}

}  // namespace CuteXmpp

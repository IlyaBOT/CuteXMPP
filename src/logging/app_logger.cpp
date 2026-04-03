#include "src/logging/app_logger.h"

#include <QDateTime>
#include <QTextStream>

#include <cstdio>

namespace CuteXmpp {

namespace {

void writeLog(const char* level, const QString& message)
{
    QTextStream stream(stdout);
    stream << '[' << level << "] "
           << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz")
           << " | " << message << Qt::endl;
    std::fflush(stdout);
}

}  // namespace

void logInfo(const QString& message)
{
    writeLog("INFO", message);
}

void logDebug(const QString& message)
{
    writeLog("DEBUG", message);
}

void logError(const QString& message)
{
    writeLog("ERROR", message);
}

QString maskSecret(const QString& value)
{
    if (value.isEmpty()) {
        return "<empty>";
    }
    return QString(value.size(), '*');
}

}  // namespace CuteXmpp

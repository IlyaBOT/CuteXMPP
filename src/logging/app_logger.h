#pragma once

#include <QString>

namespace CuteXmpp {

void logInfo(const QString& message);
void logDebug(const QString& message);
void logError(const QString& message);
QString maskSecret(const QString& value);

}  // namespace CuteXmpp

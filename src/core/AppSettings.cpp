#include "AppSettings.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QDebug>

namespace {

constexpr uint16_t DefaultLocalPort = 14540;
constexpr uint16_t DefaultRemotePort = 14550;
constexpr const char *DefaultRemoteHost = "127.0.0.1";

uint16_t portValue(QSettings &settings, const QString &key, uint16_t fallback)
{
    bool ok = false;
    const int value = settings.value(key, fallback).toInt(&ok);
    if (!ok || value < 1 || value > 65535) {
        qWarning() << "[AppSettings] 不正なポート設定:" << key << settings.value(key)
                   << "fallback:" << fallback;
        return fallback;
    }
    return static_cast<uint16_t>(value);
}

void ensureDefaults(QSettings &settings)
{
    bool changed = false;
    settings.beginGroup("mavlink");
    if (!settings.contains("local_port")) {
        settings.setValue("local_port", DefaultLocalPort);
        changed = true;
    }
    if (!settings.contains("remote_host")) {
        settings.setValue("remote_host", DefaultRemoteHost);
        changed = true;
    }
    if (!settings.contains("remote_port")) {
        settings.setValue("remote_port", DefaultRemotePort);
        changed = true;
    }
    settings.endGroup();

    if (changed) {
        settings.sync();
    }
}

} // namespace

QString AppSettings::configPath()
{
    const QString localPath = QDir::current().filePath("mavlink_sim.ini");
    if (QFile::exists(localPath)) {
        return localPath;
    }

    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (configDir.isEmpty()) {
        configDir = QCoreApplication::applicationDirPath();
    }
    QDir().mkpath(configDir);
    return QDir(configDir).filePath("mavlink_sim.ini");
}

MavlinkConnectionSettings AppSettings::loadMavlinkSettings()
{
    MavlinkConnectionSettings result;
    result.sourcePath = configPath();

    QSettings settings(result.sourcePath, QSettings::IniFormat);
    ensureDefaults(settings);

    settings.beginGroup("mavlink");
    result.localPort = portValue(settings, "local_port", DefaultLocalPort);
    result.remoteHost = settings.value("remote_host", DefaultRemoteHost).toString().trimmed();
    if (result.remoteHost.isEmpty()) {
        result.remoteHost = DefaultRemoteHost;
    }
    result.remotePort = portValue(settings, "remote_port", DefaultRemotePort);
    settings.endGroup();

    qDebug() << "[AppSettings] MAVLink設定:" << result.sourcePath
             << "local:" << result.localPort
             << "remote:" << result.remoteHost << result.remotePort;
    return result;
}

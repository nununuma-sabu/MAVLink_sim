#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QString>
#include <cstdint>

struct MavlinkConnectionSettings {
    uint16_t localPort = 14540;
    QString remoteHost = "127.0.0.1";
    uint16_t remotePort = 14550;
    QString sourcePath;
};

class AppSettings
{
public:
    static MavlinkConnectionSettings loadMavlinkSettings();
    static QString configPath();
};

#endif // APPSETTINGS_H

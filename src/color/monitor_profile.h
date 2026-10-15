#ifndef LUMINA_COLOR_MONITOR_PROFILE_H
#define LUMINA_COLOR_MONITOR_PROFILE_H

#include <QByteArray>
#include <QString>

// Bounded Windows monitor ICC query plumbing only.
//
// This file performs no colour transforms, no Canvas/renderer integration and
// no profile validation. The returned ICC bytes are handed to the caller
// untouched; interpreting the profile (sRGB class/version/maths) is owned by
// the colour-science layer.
//
// Microsoft's Advanced Color contract: a display in advanced-colour mode may
// legitimately return no profile, and the convention in that case is sRGB. This
// query does not bypass Windows colour virtualization (no registry, raw EDID or
// manual installed-profile enumeration) and does not enable the compatibility
// helper, HDR or change any OS setting.
struct MonitorProfileData {
    QString screenName;    // screen the query was issued for (as passed in)
    QString profilePath;   // returned profile file path, empty when unavailable
    QString note;          // diagnostic note; Chinese text explaining fallback
    QByteArray icc;        // raw profile bytes, empty on any failure/fallback
};

// Queries the ICC profile currently associated with a Windows display.
//
// @param screenName GDI device name, e.g. "\\\\.\\DISPLAY1".
//                   When empty, no OS query is issued and a descriptive note
//                   explaining the missing screen name is returned.
// @return profile bytes (at most maxICCbytes), profile path and screen name.
//         On missing API, missing profile or read failure, icc is empty and
//         note explains that the caller should fall back to sRGB.
MonitorProfileData readMonitorProfile(const QString &screenName);

// Upper bound for a single ICC read (4 MiB). Reads are capped at
// maxICCbytes + 1 so an oversized file is detected and rejected instead of
// being silently truncated.
constexpr int maxICCbytes = 4 * 1024 * 1024;

#endif // LUMINA_COLOR_MONITOR_PROFILE_H

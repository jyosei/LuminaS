#include "color/monitor_profile.h"

#include <QFile>

#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace {

// RAII wrapper for the HDC created by CreateDCW, so every early return in the
// query path still releases the device context.
#ifdef _WIN32
class ScopedDisplayDC {
public:
    ScopedDisplayDC() = default;
    ~ScopedDisplayDC()
    {
        if (m_dc != nullptr) {
            ::DeleteDC(m_dc);
        }
    }
    ScopedDisplayDC(const ScopedDisplayDC &) = delete;
    ScopedDisplayDC &operator=(const ScopedDisplayDC &) = delete;

    void reset(HDC dc) { m_dc = dc; }
    HDC get() const { return m_dc; }
    bool valid() const { return m_dc != nullptr; }

private:
    HDC m_dc = nullptr;
};
#endif // _WIN32

} // namespace

MonitorProfileData readMonitorProfile(const QString &screenName)
{
    MonitorProfileData result;
    result.screenName = screenName;

    QString normalized = screenName;
    if (normalized.isEmpty()) {
        result.note = QStringLiteral(
            "未提供显示器设备名，无法查询 ICC 配置文件；将按 sRGB 回退处理。");
        return result;
    }

#ifdef _WIN32
    // Own device context for the requested display. This does not associate,
    // reassociate or modify any OS profile and does not touch ICM mode.
    ScopedDisplayDC displayDc;
    displayDc.reset(::CreateDCW(L"DISPLAY",
                                reinterpret_cast<const wchar_t *>(normalized.utf16()),
                                nullptr,
                                nullptr));
    if (!displayDc.valid()) {
        result.note = QStringLiteral("无法为显示器创建 GDI 设备上下文，ICC 配置文件不可用；"
                                     "将按 sRGB 回退处理。");
        return result;
    }

    // First call: size query only (length 0, null buffer).
    DWORD requiredChars = 0;
    const BOOL queried = ::GetICMProfileW(displayDc.get(), &requiredChars, nullptr);
    // The size-only call normally returns FALSE for an insufficient buffer,
    // while still reporting the required length. That is not a missing profile.
    if (!queried && requiredChars == 0) {
        result.note = QStringLiteral("当前显示器没有可用的 ICC 配置文件"
                                     "（高级颜色模式可能不返回配置文件），"
                                     "将按 sRGB 回退处理。");
        return result;
    }

    if (requiredChars < 1 || requiredChars > 32768) {
        result.note = QStringLiteral("显示器 ICC 配置文件路径长度异常，ICC 配置文件不可用；"
                                     "将按 sRGB 回退处理。");
        return result;
    }

    // Second call: bounded UTF-16 buffer, GetICMProfileW writes a terminated path.
    std::vector<wchar_t> pathBuffer(static_cast<size_t>(requiredChars) + 1, L'\0');
    if (!::GetICMProfileW(displayDc.get(),
                          &requiredChars,
                          pathBuffer.data())) {
        result.note = QStringLiteral("读取显示器 ICC 配置文件路径失败，"
                                     "将按 sRGB 回退处理。");
        return result;
    }

    const QString profilePath = QString::fromWCharArray(pathBuffer.data());
    if (profilePath.isEmpty()) {
        result.note = QStringLiteral("显示器 ICC 配置文件路径为空，"
                                     "将按 sRGB 回退处理。");
        return result;
    }

    QFile profileFile(profilePath);
    if (!profileFile.exists()) {
        result.note = QStringLiteral("显示器 ICC 配置文件不存在，"
                                     "将按 sRGB 回退处理。");
        return result;
    }
    if (!profileFile.open(QIODevice::ReadOnly)) {
        result.note = QStringLiteral("无法读取显示器 ICC 配置文件，"
                                     "将按 sRGB 回退处理。");
        return result;
    }

    const qint64 declaredSize = profileFile.size();
    if (declaredSize <= 0 || declaredSize > static_cast<qint64>(maxICCbytes)) {
        result.note = QStringLiteral("显示器 ICC 配置文件大小无效或超出上限，"
                                     "将按 sRGB 回退处理。");
        return result;
    }

    // Cap the read at maxICCbytes + 1 and require the full declared size, so a
    // file that changes size or grows past the bound is rejected, not truncated.
    const QByteArray bytes = profileFile.read(static_cast<qint64>(maxICCbytes) + 1);
    if (bytes.size() != declaredSize || profileFile.size() != declaredSize) {
        result.note = QStringLiteral("显示器 ICC 配置文件读取不完整或读取期间发生变化，"
                                     "将按 sRGB 回退处理。");
        return result;
    }

    // Bytes and path are returned as-is: no validation, no transformation.
    result.icc = bytes;
    result.profilePath = profilePath;
    result.note = QStringLiteral("已读取显示器 ICC 配置文件原始字节，未经校验或转换。");
    return result;
#else
    result.note = QStringLiteral("当前平台不支持自动查询显示器 ICC 配置文件；"
                                 "将按 sRGB 回退处理。");
    return result;
#endif // _WIN32
}

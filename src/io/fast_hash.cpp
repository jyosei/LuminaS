// src/io/fast_hash.cpp
#include "fast_hash.h"

#include <QCryptographicHash>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <bcrypt.h>
#endif

namespace {

#ifdef Q_OS_WIN

// Upper bound for the BCrypt hash object buffer; guards against absurd values.
constexpr ULONG kMaxHashObjectBytes = 1u * 1024u * 1024u;

// Chunk size for HashData; kept well below ULONG_MAX (16 MiB).
constexpr ULONG kMaxChunkBytes = 16u * 1024u * 1024u;

// RAII for the BCrypt algorithm provider handle.
class AlgorithmProvider {
public:
    AlgorithmProvider() = default;
    ~AlgorithmProvider() { close(); }

    AlgorithmProvider(const AlgorithmProvider &) = delete;
    AlgorithmProvider &operator=(const AlgorithmProvider &) = delete;

    bool open() {
        return BCryptOpenAlgorithmProvider(&m_handle,
                                           BCRYPT_SHA256_ALGORITHM,
                                           nullptr,
                                           0) == 0;
    }

    void close() {
        if (m_handle) {
            BCryptCloseAlgorithmProvider(m_handle, 0);
            m_handle = nullptr;
        }
    }

    BCRYPT_ALG_HANDLE handle() const { return m_handle; }

private:
    BCRYPT_ALG_HANDLE m_handle = nullptr;
};

// RAII for the BCrypt hash object handle.
class HashHandle {
public:
    HashHandle() = default;
    ~HashHandle() { destroy(); }

    HashHandle(const HashHandle &) = delete;
    HashHandle &operator=(const HashHandle &) = delete;

    void destroy() {
        if (m_handle) {
            BCryptDestroyHash(m_handle);
            m_handle = nullptr;
        }
    }

    BCRYPT_HASH_HANDLE *out() { return &m_handle; }
    BCRYPT_HASH_HANDLE handle() const { return m_handle; }

private:
    BCRYPT_HASH_HANDLE m_handle = nullptr;
};

// Attempts the native SHA-256 computation. Returns true only on full success.
bool tryNativeSha256(QByteArrayView data, QByteArray &out) {
    AlgorithmProvider provider;
    if (!provider.open())
        return false;

    ULONG objectLength = 0;
    ULONG resultLength = 0;
    if (BCryptGetProperty(provider.handle(),
                          BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&objectLength),
                          sizeof(objectLength),
                          &resultLength,
                          0) != 0) {
        return false;
    }
    if (resultLength != sizeof(objectLength))
        return false;
    if (objectLength == 0 || objectLength > kMaxHashObjectBytes)
        return false;

    ULONG hashLength = 0;
    resultLength = 0;
    if (BCryptGetProperty(provider.handle(),
                          BCRYPT_HASH_LENGTH,
                          reinterpret_cast<PUCHAR>(&hashLength),
                          sizeof(hashLength),
                          &resultLength,
                          0) != 0) {
        return false;
    }
    if (resultLength != sizeof(hashLength))
        return false;
    if (hashLength != 32u)
        return false;

    QByteArray objectBuffer;
    objectBuffer.resize(static_cast<int>(objectLength));
    if (objectBuffer.size() != static_cast<int>(objectLength))
        return false;

    HashHandle hash;
    if (BCryptCreateHash(provider.handle(),
                         hash.out(),
                         reinterpret_cast<PUCHAR>(objectBuffer.data()),
                         objectLength,
                         nullptr,
                         0,
                         0) != 0) {
        return false;
    }

    // Feed data in chunks small enough to keep ULONG sizes well defined.
    const char *ptr = data.data();
    qsizetype remaining = data.size();
    while (remaining > 0) {
        const ULONG chunk = (remaining > static_cast<qsizetype>(kMaxChunkBytes))
                                ? kMaxChunkBytes
                                : static_cast<ULONG>(remaining);
        if (BCryptHashData(hash.handle(),
                           reinterpret_cast<PUCHAR>(const_cast<char *>(ptr)),
                           chunk,
                           0) != 0) {
            return false;
        }
        ptr += chunk;
        remaining -= static_cast<qsizetype>(chunk);
    }

    out.resize(static_cast<int>(hashLength));
    if (out.size() != static_cast<int>(hashLength))
        return false;

    if (BCryptFinishHash(hash.handle(),
                         reinterpret_cast<PUCHAR>(out.data()),
                         hashLength,
                         0) != 0) {
        return false;
    }

    return true;
}

#endif // Q_OS_WIN

} // namespace

QByteArray fastSha256(QByteArrayView data, bool *usedNative)
{
#ifdef Q_OS_WIN
    {
        QByteArray native;
        if (tryNativeSha256(data, native)) {
            if (usedNative)
                *usedNative = true;
            return native;
        }
    }
#endif

    if (usedNative)
        *usedNative = false;
    return QCryptographicHash::hash(data,
                                    QCryptographicHash::Sha256);
}

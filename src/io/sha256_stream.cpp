// src/io/sha256_stream.cpp
#include "sha256_stream.h"

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
constexpr ULONG kMaxHashObjectBytes = 1u * 1024u * 1024u; // 1 MiB cap
constexpr ULONG kMaxChunkBytes      = 16u * 1024u * 1024u;
#endif

} // namespace

struct Sha256Stream::Impl
{
    ~Impl(); // Own native cleanup here: move assignment destroys the old pimpl.
    enum class Backend { Qt, Native };

    Backend backend = Backend::Qt;
    bool finalized = false;
    bool failed    = false;
    QByteArray digest;             // cached once finalized && !failed
    std::unique_ptr<QCryptographicHash> qt;

#ifdef Q_OS_WIN
    BCRYPT_ALG_HANDLE prov   = nullptr;
    BCRYPT_HASH_HANDLE hash  = nullptr;
    QByteArray objectBuffer;       // owned for lifetime of hash handle

    bool initNative();
    void destroyNative();          // destroys hash, buffer, then provider
    bool addNative(QByteArrayView);
    bool finishNative();
#endif

    bool initQt();
    bool addQt(QByteArrayView);
    bool finishQt();
};

Sha256Stream::Impl::~Impl()
{
#ifdef Q_OS_WIN
    destroyNative();
#endif
}

// ---- Qt backend -----------------------------------------------------------

bool Sha256Stream::Impl::initQt()
{
    qt = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    return qt != nullptr;
}

bool Sha256Stream::Impl::addQt(QByteArrayView chunk)
{
    if (!qt)
        return false;
    if (!chunk.isEmpty()) // Qt tolerates empty pointer; skip for clarity
        qt->addData(chunk);
    return true;
}

bool Sha256Stream::Impl::finishQt()
{
    if (!qt)
        return false;
    digest = qt->result();
    return digest.size() == 32;
}

#ifdef Q_OS_WIN

// ---- Native backend -------------------------------------------------------

void Sha256Stream::Impl::destroyNative()
{
    if (hash) {
        BCryptDestroyHash(hash);
        hash = nullptr;
    }
    objectBuffer = QByteArray(); // release before (or with) provider teardown
    if (prov) {
        BCryptCloseAlgorithmProvider(prov, 0);
        prov = nullptr;
    }
}

bool Sha256Stream::Impl::initNative()
{
    if (BCryptOpenAlgorithmProvider(&prov, BCRYPT_SHA256_ALGORITHM,
                                    nullptr, 0) != 0) {
        destroyNative();
        return false;
    }

    ULONG objectLength = 0, resultLength = 0;
    if (BCryptGetProperty(prov, BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&objectLength),
                          sizeof(objectLength), &resultLength, 0) != 0 ||
        resultLength != sizeof(objectLength) ||
        objectLength == 0 || objectLength > kMaxHashObjectBytes) {
        destroyNative();
        return false;
    }

    ULONG hashLength = 0;
    resultLength = 0;
    if (BCryptGetProperty(prov, BCRYPT_HASH_LENGTH,
                          reinterpret_cast<PUCHAR>(&hashLength),
                          sizeof(hashLength), &resultLength, 0) != 0 ||
        resultLength != sizeof(hashLength) ||
        hashLength != 32u) {
        destroyNative();
        return false;
    }

    objectBuffer.resize(static_cast<int>(objectLength));
    if (objectBuffer.size() != static_cast<int>(objectLength)) {
        destroyNative();
        return false;
    }

    if (BCryptCreateHash(prov, &hash,
                         reinterpret_cast<PUCHAR>(objectBuffer.data()),
                         objectLength, nullptr, 0, 0) != 0) {
        hash = nullptr;
        destroyNative();
        return false;
    }
    return true;
}

bool Sha256Stream::Impl::addNative(QByteArrayView chunk)
{
    if (!hash)
        return false;
    const char *ptr = chunk.data();
    qsizetype remaining = chunk.size();
    while (remaining > 0) {
        const ULONG n = (remaining > static_cast<qsizetype>(kMaxChunkBytes))
                            ? kMaxChunkBytes
                            : static_cast<ULONG>(remaining);
        if (BCryptHashData(hash,
                           reinterpret_cast<PUCHAR>(const_cast<char *>(ptr)),
                           n, 0) != 0) {
            return false;
        }
        ptr += n;
        remaining -= static_cast<qsizetype>(n);
    }
    return true;
}

bool Sha256Stream::Impl::finishNative()
{
    if (!hash)
        return false;
    QByteArray out(32, Qt::Uninitialized);
    if (BCryptFinishHash(hash, reinterpret_cast<PUCHAR>(out.data()),
                         32, 0) != 0) {
        return false;
    }
    digest = std::move(out);
    return true;
}

#endif // Q_OS_WIN

// ---- Public API -----------------------------------------------------------

Sha256Stream::Sha256Stream(bool preferNative)
    : m_impl(std::make_unique<Impl>())
{
#ifdef Q_OS_WIN
    if (preferNative) {
        if (m_impl->initNative()) {
            m_impl->backend = Impl::Backend::Native;
            return;
        }
        // Native init failed before any data: safe to fall back to Qt.
        m_impl->destroyNative();
    }
#endif
    Q_UNUSED(preferNative);
    m_impl->backend = Impl::Backend::Qt;
    if (!m_impl->initQt())
        m_impl->failed = true;
}

Sha256Stream::~Sha256Stream() = default;

Sha256Stream::Sha256Stream(Sha256Stream &&other) noexcept = default;
Sha256Stream &Sha256Stream::operator=(Sha256Stream &&other) noexcept = default;

bool Sha256Stream::addData(QByteArrayView chunk)
{
    if (!m_impl || m_impl->failed || m_impl->finalized)
        return false;

    bool ok = false;
    switch (m_impl->backend) {
    case Impl::Backend::Qt:     ok = m_impl->addQt(chunk);     break;
#ifdef Q_OS_WIN
    case Impl::Backend::Native: ok = m_impl->addNative(chunk); break;
#endif
    }

    if (!ok) {
        // Cannot restart a different backend: the accepted prefix would be
        // lost. Invalidate permanently.
        m_impl->failed = true;
        m_impl->digest.clear();
#ifdef Q_OS_WIN
        m_impl->destroyNative();
#endif
        return false;
    }
    return true;
}

QByteArray Sha256Stream::result()
{
    if (!m_impl || m_impl->failed)
        return QByteArray();
    if (m_impl->finalized)
        return m_impl->digest;

    bool ok = false;
    switch (m_impl->backend) {
    case Impl::Backend::Qt:     ok = m_impl->finishQt();     break;
#ifdef Q_OS_WIN
    case Impl::Backend::Native: ok = m_impl->finishNative(); break;
#endif
    }

    if (!ok || m_impl->digest.size() != 32) {
        m_impl->failed = true;
        m_impl->digest.clear();
#ifdef Q_OS_WIN
        m_impl->destroyNative();
#endif
        return QByteArray();
    }

    m_impl->finalized = true;
#ifdef Q_OS_WIN
    m_impl->destroyNative(); // release handles eagerly once finalized
#endif
    return m_impl->digest;
}

bool Sha256Stream::ok() const
{
    return m_impl && !m_impl->failed;
}

bool Sha256Stream::native() const
{
#ifdef Q_OS_WIN
    return m_impl && !m_impl->failed
        && m_impl->backend == Impl::Backend::Native;
#else
    return false;
#endif
}

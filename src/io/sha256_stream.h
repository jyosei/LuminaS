// src/io/sha256_stream.h
#pragma once

#include <QByteArray>
#include <QByteArrayView>

#include <memory>

// Incremental SHA-256, digest-identical to
// QCryptographicHash::hash(..., Sha256) over the concatenated input.
//
// Backend: on Windows, BCrypt-backed streaming when available; otherwise the
// Qt streaming backend. If preferNative is false, the Qt backend is used
// unconditionally (explicit reference path; no environment variables).
//
// Semantics:
//  - The pimpl backend is chosen at construction, before any data is fed.
//  - Once the native backend has accepted at least one addData()/partial init
//    and then fails, the instance becomes permanently invalid: ok() stays
//    false, result() returns an empty QByteArray, addData() returns false. It
//    never restarts a different backend mid-stream (that would silently drop
//    the already-fed prefix). No data is ever fed to two backends at once.
//  - Empty input yields the canonical 32-byte SHA-256 empty-string digest.
//  - result() finalizes on first call and caches the digest; subsequent calls
//    return the same cached bytes. addData() after finalization returns false
//    and leaves the cached digest untouched.
//  - Moved-from instances are safe: ok()==false, native()==false,
//    result() is empty, addData() returns false.
//  - Each instance owns its own native provider/hash handles; no global
//    mutable state. Instances are not internally synchronized: use one per
//    thread (or externally serialize).
class Sha256Stream
{
public:
    explicit Sha256Stream(bool preferNative = true);
    ~Sha256Stream();

    Sha256Stream(Sha256Stream &&other) noexcept;
    Sha256Stream &operator=(Sha256Stream &&other) noexcept;

    Sha256Stream(const Sha256Stream &) = delete;
    Sha256Stream &operator=(const Sha256Stream &) = delete;

    // Returns false if the stream is invalid, finalized, or the backend
    // reported a failure. On false, prior accepted bytes are discarded from
    // the result path (the instance is invalidated on backend failure).
    bool addData(QByteArrayView chunk);

    // Finalizes on first call and caches the 32-byte digest. Returns an empty
    // QByteArray if the instance is invalid or moved-from.
    QByteArray result();

    // False once invalid, finalized-after-failure, or moved-from.
    bool ok() const;

    // True iff the native backend is in use and still healthy.
    bool native() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

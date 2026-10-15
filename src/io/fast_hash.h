// src/io/fast_hash.h
#pragma once

#include <QByteArray>
#include <QByteArrayView>

// Computes the SHA-256 digest of `data`.
//
// Platform glue only: on Windows this attempts a BCrypt-backed computation and
// transparently falls back to QCryptographicHash::hash(data, Sha256) on any
// native failure. On non-Windows it always uses the Qt backend.
//
// Guarantees:
//  - The returned 32-byte digest is byte-identical to
//    QCryptographicHash::hash(data, QCryptographicHash::Sha256).
//  - Empty input yields the standard SHA-256 digest of the empty string.
//  - `data` is never modified.
//  - No shared mutable native state; each call owns its provider/hash objects.
//
// If `usedNative` is non-null it is set to true only when the native path ran
// to full success, and false otherwise (including the non-Windows case).
QByteArray fastSha256(QByteArrayView data, bool *usedNative = nullptr);

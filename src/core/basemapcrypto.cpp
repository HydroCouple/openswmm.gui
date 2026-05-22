/*!
 * \file   basemapcrypto.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */
#include "core/basemapcrypto.h"

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QSysInfo>

#include <openssl/evp.h>
#include <openssl/rand.h>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static constexpr int kKeyLen      = 32;   // AES-256 key
static constexpr int kIvLen       = 16;   // AES-CBC IV
static constexpr int kSaltLen     = 16;
static constexpr int kIterations  = 100000;

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

QByteArray BasemapCrypto::machinePassphrase()
{
    const QByteArray raw = QSysInfo::machineUniqueId() + "OpenSWMM-BasemapStore-v1";
    return QCryptographicHash::hash(raw, QCryptographicHash::Sha256).toHex();
}

bool BasemapCrypto::deriveKey(const QByteArray &passphrase,
                               const QByteArray &salt,
                               QByteArray       &key,
                               QByteArray       &iv)
{
    key.resize(kKeyLen);
    iv.resize(kIvLen);

    const int ok = PKCS5_PBKDF2_HMAC(
        passphrase.constData(), passphrase.size(),
        reinterpret_cast<const unsigned char *>(salt.constData()), salt.size(),
        kIterations,
        EVP_sha256(),
        kKeyLen, reinterpret_cast<unsigned char *>(key.data()));

    if (ok != 1) return false;

    // Derive IV via a second PBKDF2 pass with a different "salt prefix"
    QByteArray ivSalt = QByteArray("iv-") + salt;
    const int okIv = PKCS5_PBKDF2_HMAC(
        passphrase.constData(), passphrase.size(),
        reinterpret_cast<const unsigned char *>(ivSalt.constData()), ivSalt.size(),
        kIterations,
        EVP_sha256(),
        kIvLen, reinterpret_cast<unsigned char *>(iv.data()));

    return okIv == 1;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

QByteArray BasemapCrypto::encrypt(const QString &plaintext)
{
    if (plaintext.isEmpty()) return {};

    const QByteArray pass = machinePassphrase();

    // Random salt
    QByteArray salt(kSaltLen, '\0');
    if (RAND_bytes(reinterpret_cast<unsigned char *>(salt.data()), kSaltLen) != 1)
        return {};

    QByteArray key, iv;
    if (!deriveKey(pass, salt, key, iv)) return {};

    const QByteArray input = plaintext.toUtf8();

    // Allocate output buffer (plaintext + one extra block for padding)
    QByteArray cipherBuf(input.size() + EVP_MAX_BLOCK_LENGTH, '\0');
    int outLen1 = 0, outLen2 = 0;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};

    bool ok =
        EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
                           reinterpret_cast<const unsigned char *>(key.constData()),
                           reinterpret_cast<const unsigned char *>(iv.constData())) == 1
        &&
        EVP_EncryptUpdate(ctx,
                          reinterpret_cast<unsigned char *>(cipherBuf.data()), &outLen1,
                          reinterpret_cast<const unsigned char *>(input.constData()), input.size()) == 1
        &&
        EVP_EncryptFinal_ex(ctx,
                            reinterpret_cast<unsigned char *>(cipherBuf.data()) + outLen1, &outLen2) == 1;

    EVP_CIPHER_CTX_free(ctx);
    if (!ok) return {};

    cipherBuf.resize(outLen1 + outLen2);

    // Format: salt (16) | ciphertext
    QByteArray payload = salt + cipherBuf;
    return payload.toBase64();
}

QString BasemapCrypto::decrypt(const QByteArray &cipherBase64)
{
    if (cipherBase64.isEmpty()) return {};

    const QByteArray payload = QByteArray::fromBase64(cipherBase64);
    if (payload.size() <= kSaltLen) return {};

    const QByteArray salt       = payload.left(kSaltLen);
    const QByteArray cipherData = payload.mid(kSaltLen);

    const QByteArray pass = machinePassphrase();

    QByteArray key, iv;
    if (!deriveKey(pass, salt, key, iv)) return {};

    QByteArray plainBuf(cipherData.size() + EVP_MAX_BLOCK_LENGTH, '\0');
    int outLen1 = 0, outLen2 = 0;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};

    bool ok =
        EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
                           reinterpret_cast<const unsigned char *>(key.constData()),
                           reinterpret_cast<const unsigned char *>(iv.constData())) == 1
        &&
        EVP_DecryptUpdate(ctx,
                          reinterpret_cast<unsigned char *>(plainBuf.data()), &outLen1,
                          reinterpret_cast<const unsigned char *>(cipherData.constData()), cipherData.size()) == 1
        &&
        EVP_DecryptFinal_ex(ctx,
                            reinterpret_cast<unsigned char *>(plainBuf.data()) + outLen1, &outLen2) == 1;

    EVP_CIPHER_CTX_free(ctx);
    if (!ok) return {};

    plainBuf.resize(outLen1 + outLen2);
    return QString::fromUtf8(plainBuf);
}

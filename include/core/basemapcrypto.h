/**
 * @file      basemapcrypto.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @brief     AES-256-CBC encryption helper for basemap connection passwords.
 *
 * @details
 * Provides a simple encrypt/decrypt API backed by OpenSSL EVP_* (available
 * via Qt6's Network dependency which already links OpenSSL).
 *
 * Key derivation: PBKDF2-SHA256, 100,000 iterations.
 * Per-machine passphrase:
 *   QCryptographicHash::hash(
 *       QSysInfo::machineUniqueId() + "OpenSWMM-BasemapStore-v1", SHA256
 *   ).toHex()
 *
 * Cipher output format (all stored Base64):
 *   [ 16-byte salt | ciphertext ]; the IV is derived from the salt
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef BASEMAPCRYPTO_H
#define BASEMAPCRYPTO_H

#include <QByteArray>
#include <QString>

/*!
 * \class BasemapCrypto
 * \brief Static helper for AES-256-CBC encrypt/decrypt of short strings.
 */
class BasemapCrypto
{
public:
    /**
     * @brief Disable construction; all operations are static.
     */
    BasemapCrypto() = delete;

    /*!
     * \brief Encrypts \p plaintext using a machine-derived passphrase.
     * \return Base64-encoded ciphertext, or an empty QByteArray on failure.
     * @param[in] plaintext UTF-16 text converted to UTF-8 before encryption; empty input returns empty output.
     * @details Generates a fresh 16-byte random salt for each call and derives the key/IV from the machine passphrase. An empty result also represents random-generator, key-derivation or cipher failure.
     */
    static QByteArray encrypt(const QString &plaintext);

    /*!
     * \brief Decrypts \p cipherBase64 using a machine-derived passphrase.
     * \return Decrypted plaintext, or an empty QString on failure or
     *         when the passphrase does not match (wrong machine).
     * @param[in] cipherBase64 Encoded payload produced by encrypt(); malformed, short or empty payloads return empty text.
     * @details Uses this machine's derived passphrase. Empty output cannot distinguish failure from an empty plaintext.
     */
    static QString decrypt(const QByteArray &cipherBase64);

private:
    /**
     * @brief Hash the machine identifier and application domain string into a local passphrase.
     * @return SHA-256 digest encoded as hexadecimal bytes; depends on QSysInfo::machineUniqueId().
     */
    static QByteArray machinePassphrase();
    /**
     * @brief Derive AES key and IV using independent PBKDF2-SHA256 salt domains.
     * @param[in] passphrase Machine-derived passphrase bytes.
     * @param[in] salt Salt from the ciphertext payload (16 bytes for this format).
     * @param[out] key Resized to 32 bytes before derivation; unusable on false.
     * @param[out] iv Resized to 16 bytes; derived using the iv- prefixed salt and unusable on false.
     * @return True only if both 100,000-iteration PBKDF2 calls succeed.
     */
    static bool deriveKey(const QByteArray &passphrase, const QByteArray &salt, QByteArray &key,
                          QByteArray &iv);
};

#endif // BASEMAPCRYPTO_H

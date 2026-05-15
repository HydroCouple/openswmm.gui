/*!
 * \file   basemapcrypto.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \brief  AES-256-CBC encryption helper for basemap connection passwords.
 *
 * \details
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
 *   [ 16-byte salt | 16-byte IV | ciphertext ]
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
    BasemapCrypto() = delete;

    /*!
     * \brief Encrypts \p plaintext using a machine-derived passphrase.
     * \return Base64-encoded ciphertext, or an empty QByteArray on failure.
     */
    static QByteArray encrypt(const QString &plaintext);

    /*!
     * \brief Decrypts \p cipherBase64 using a machine-derived passphrase.
     * \return Decrypted plaintext, or an empty QString on failure or
     *         when the passphrase does not match (wrong machine).
     */
    static QString decrypt(const QByteArray &cipherBase64);

private:
    static QByteArray machinePassphrase();
    static bool deriveKey(const QByteArray &passphrase,
                          const QByteArray &salt,
                          QByteArray       &key,
                          QByteArray       &iv);
};

#endif // BASEMAPCRYPTO_H

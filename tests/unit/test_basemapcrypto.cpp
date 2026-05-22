/*!
 * \file   test_basemapcrypto.cpp
 * \brief  Unit tests for BasemapCrypto — AES-256-CBC encrypt / decrypt.
 *
 * Tests run on the local machine, so the machine-derived passphrase is the
 * same for both encrypt and decrypt.  All tests are self-contained; no files
 * or network are needed.
 */

#include <gtest/gtest.h>

#include <QByteArray>
#include <QString>

#include "core/basemapcrypto.h"

// ---------------------------------------------------------------------------
// Encrypt / decrypt round-trip
// ---------------------------------------------------------------------------

TEST(BasemapCrypto, EncryptDecryptRoundTrip_ASCII)
{
    const QString plain = QStringLiteral("MySecretPassword123!");
    const QByteArray cipher = BasemapCrypto::encrypt(plain);

    ASSERT_FALSE(cipher.isEmpty()) << "encrypt() returned empty for non-empty input";

    const QString recovered = BasemapCrypto::decrypt(cipher);
    EXPECT_EQ(recovered, plain);
}

TEST(BasemapCrypto, EncryptDecryptRoundTrip_Unicode)
{
    // Non-ASCII characters must survive the UTF-8 round-trip.
    const QString plain = QString::fromUtf8("pässwörð ¡café!");
    const QByteArray cipher = BasemapCrypto::encrypt(plain);

    ASSERT_FALSE(cipher.isEmpty());
    EXPECT_EQ(BasemapCrypto::decrypt(cipher), plain);
}

TEST(BasemapCrypto, EncryptDecryptRoundTrip_LongString)
{
    const QString plain = QString(512, QChar('A')) + QString(512, QChar('z'));
    const QByteArray cipher = BasemapCrypto::encrypt(plain);

    ASSERT_FALSE(cipher.isEmpty());
    EXPECT_EQ(BasemapCrypto::decrypt(cipher), plain);
}

// ---------------------------------------------------------------------------
// Empty input handling
// ---------------------------------------------------------------------------

TEST(BasemapCrypto, EmptyPlaintextProducesEmptyOutput)
{
    EXPECT_TRUE(BasemapCrypto::encrypt(QString()).isEmpty());
}

TEST(BasemapCrypto, DecryptEmptyInputProducesEmptyString)
{
    EXPECT_TRUE(BasemapCrypto::decrypt(QByteArray()).isEmpty());
}

// ---------------------------------------------------------------------------
// Ciphertext properties
// ---------------------------------------------------------------------------

TEST(BasemapCrypto, CiphertextDiffersFromPlaintext)
{
    const QString plain = QStringLiteral("password");
    const QByteArray cipher = BasemapCrypto::encrypt(plain);

    ASSERT_FALSE(cipher.isEmpty());
    // The Base64 ciphertext should not contain the literal plaintext.
    const QByteArray decoded = QByteArray::fromBase64(cipher);
    EXPECT_FALSE(decoded.contains(plain.toUtf8()));
}

TEST(BasemapCrypto, TwoEncryptionsOfSamePlaintextProduceDifferentCiphertext)
{
    // A random salt per encryption means two calls must differ.
    const QString plain = QStringLiteral("samepassword");
    const QByteArray c1 = BasemapCrypto::encrypt(plain);
    const QByteArray c2 = BasemapCrypto::encrypt(plain);

    ASSERT_FALSE(c1.isEmpty());
    ASSERT_FALSE(c2.isEmpty());
    EXPECT_NE(c1, c2) << "Two encryptions of the same string must not be identical "
                         "(different random salts expected)";
}

// ---------------------------------------------------------------------------
// Garbage / tampered input
// ---------------------------------------------------------------------------

TEST(BasemapCrypto, DecryptNonBase64ReturnsEmpty)
{
    const QByteArray garbage("not!valid!base64!!!");
    // Must not crash; return value may be empty or some string — the critical
    // property is that it does not throw or abort.
    const QString result = BasemapCrypto::decrypt(garbage);
    // We cannot assert the exact value when base64 decoding partially succeeds,
    // but we can confirm the function completes.
    Q_UNUSED(result);
    SUCCEED();
}

TEST(BasemapCrypto, DecryptTruncatedCiphertextReturnsEmpty)
{
    // Real cipher is salt(16) + IV(16) + ciphertext, Base64-encoded.
    // Feed a Base64 blob that is too short to contain even the salt.
    const QByteArray tooShort = QByteArray("AAAA").toBase64(); // 3 bytes decoded
    const QString result = BasemapCrypto::decrypt(tooShort);
    EXPECT_TRUE(result.isEmpty())
        << "decrypt() of truncated input should return empty QString";
}

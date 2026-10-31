/*
 * QEMU Crypto cipher algorithms
 *
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef QCRYPTO_CIPHER_H
#define QCRYPTO_CIPHER_H

#include "qapi/qapi-types-crypto.h"

typedef struct QCryptoCipher QCryptoCipher;
typedef struct QCryptoCipherDriver QCryptoCipherDriver;

/* See also "QCryptoCipherAlgo" and "QCryptoCipherMode"
 * enums defined in qapi/crypto.json */

/**
 * QCryptoCipher:
 *
 * The QCryptoCipher object provides a way to perform encryption
 * and decryption of data, with a standard API, regardless of the
 * algorithm used. It further isolates the calling code from the
 * details of the specific underlying implementation, whether
 * built-in, libgcrypt or nettle.
 *
 * Each QCryptoCipher object is capable of performing both
 * encryption and decryption, and can operate in a number
 * or modes including ECB, CBC.
 *
 * <example>
 *   <title>Encrypting data with AES-128 in CBC mode</title>
 *   <programlisting>
 * QCryptoCipher *cipher;
 * uint8_t key = ....;
 * size_t keylen = 16;
 * uint8_t iv = ....;
 *
 * if (!qcrypto_cipher_supports(QCRYPTO_CIPHER_ALGO_AES_128)) {
 *    error_report(errp, "Feature <blah> requires AES cipher support");
 *    return -1;
 * }
 *
 * cipher = qcrypto_cipher_new(QCRYPTO_CIPHER_ALGO_AES_128,
 *                             QCRYPTO_CIPHER_MODE_CBC,
 *                             key, keylen,
 *                             errp);
 * if (!cipher) {
 *    return -1;
 * }
 *
 * if (qcrypto_cipher_set_iv(cipher, iv, keylen, errp) < 0) {
 *    return -1;
 * }
 *
 * if (qcrypto_cipher_encrypt(cipher, rawdata, encdata, datalen, errp) < 0) {
 *    return -1;
 * }
 *
 * qcrypto_cipher_free(cipher);
 *   </programlisting>
 * </example>
 *
 */

struct QCryptoCipher {
    QCryptoCipherAlgo alg;
    QCryptoCipherMode mode;
    const QCryptoCipherDriver *driver;
};




/**
 * qcrypto_cipher_free:
 * @cipher: the cipher object
 *
 * Release the memory associated with @cipher that
 * was previously allocated by qcrypto_cipher_new()
 */
void qcrypto_cipher_free(QCryptoCipher *cipher);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(QCryptoCipher, qcrypto_cipher_free)



#endif /* QCRYPTO_CIPHER_H */

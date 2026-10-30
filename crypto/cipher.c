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

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "qapi/error.h"
#include "crypto/cipher.h"
#include "cipherpriv.h"


static const size_t alg_key_len[QCRYPTO_CIPHER_ALGO__MAX] = {
    [QCRYPTO_CIPHER_ALGO_AES_128] = 16,
    [QCRYPTO_CIPHER_ALGO_AES_192] = 24,
    [QCRYPTO_CIPHER_ALGO_AES_256] = 32,
    [QCRYPTO_CIPHER_ALGO_DES] = 8,
    [QCRYPTO_CIPHER_ALGO_3DES] = 24,
    [QCRYPTO_CIPHER_ALGO_CAST5_128] = 16,
    [QCRYPTO_CIPHER_ALGO_SERPENT_128] = 16,
    [QCRYPTO_CIPHER_ALGO_SERPENT_192] = 24,
    [QCRYPTO_CIPHER_ALGO_SERPENT_256] = 32,
    [QCRYPTO_CIPHER_ALGO_TWOFISH_128] = 16,
    [QCRYPTO_CIPHER_ALGO_TWOFISH_192] = 24,
    [QCRYPTO_CIPHER_ALGO_TWOFISH_256] = 32,
#ifdef CONFIG_CRYPTO_SM4
    [QCRYPTO_CIPHER_ALGO_SM4] = 16,
#endif
};





static bool
qcrypto_cipher_validate_key_length(QCryptoCipherAlgo alg,
                                   QCryptoCipherMode mode,
                                   size_t nkey,
                                   Error **errp)
{
    if ((unsigned)alg >= QCRYPTO_CIPHER_ALGO__MAX) {
        error_setg(errp, "Cipher algorithm %d out of range",
                   alg);
        return false;
    }

    if (mode == QCRYPTO_CIPHER_MODE_XTS) {
        if (alg == QCRYPTO_CIPHER_ALGO_DES ||
            alg == QCRYPTO_CIPHER_ALGO_3DES) {
            error_setg(errp, "XTS mode not compatible with DES/3DES");
            return false;
        }
        if (nkey % 2) {
            error_setg(errp, "XTS cipher key length should be a multiple of 2");
            return false;
        }

        if (alg_key_len[alg] != (nkey / 2)) {
            error_setg(errp, "Cipher key length %zu should be %zu",
                       nkey, alg_key_len[alg] * 2);
            return false;
        }
    } else {
        if (alg_key_len[alg] != nkey) {
            error_setg(errp, "Cipher key length %zu should be %zu",
                       nkey, alg_key_len[alg]);
            return false;
        }
    }
    return true;
}

#ifdef CONFIG_GCRYPT
#include "cipher-gcrypt.c.inc"
#elif defined CONFIG_NETTLE
#include "cipher-nettle.c.inc"
#elif defined CONFIG_GNUTLS
#include "cipher-gnutls.c.inc"
#else
#include "cipher-stub.c.inc"
#endif

QCryptoCipher *qcrypto_cipher_new(QCryptoCipherAlgo alg,
                                  QCryptoCipherMode mode,
                                  const uint8_t *key, size_t nkey,
                                  Error **errp)
{
    QCryptoCipher *cipher = NULL;

#ifdef CONFIG_AF_ALG
    cipher = qcrypto_afalg_cipher_ctx_new(alg, mode, key, nkey, NULL);
#endif

    if (!cipher) {
        cipher = qcrypto_cipher_ctx_new(alg, mode, key, nkey, errp);
        if (!cipher) {
            return NULL;
        }
    }

    cipher->alg = alg;
    cipher->mode = mode;

    return cipher;
}



int qcrypto_cipher_decrypt(QCryptoCipher *cipher,
                           const void *in,
                           void *out,
                           size_t len,
                           Error **errp)
{
    const QCryptoCipherDriver *drv = cipher->driver;
    return drv->cipher_decrypt(cipher, in, out, len, errp);
}


int qcrypto_cipher_setiv(QCryptoCipher *cipher,
                         const uint8_t *iv, size_t niv,
                         Error **errp)
{
    const QCryptoCipherDriver *drv = cipher->driver;
    return drv->cipher_setiv(cipher, iv, niv, errp);
}


void qcrypto_cipher_free(QCryptoCipher *cipher)
{
    if (cipher) {
        cipher->driver->cipher_free(cipher);
    }
}

/*
 * QEMU crypto TLS session support
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
#include "qemu/error-report.h"
#include "qemu/thread.h"
#include "crypto/tlssession.h"
#include "crypto/tlscredsanon.h"
#include "crypto/tlscredspsk.h"
#include "crypto/tlscredsx509.h"
#include "qapi/error.h"
#include "authz/base.h"
#include "tlscredspriv.h"
#include "trace.h"

#ifdef CONFIG_GNUTLS


#include <gnutls/x509.h>


struct QCryptoTLSSession {
    QCryptoTLSCreds *creds;
    QCryptoTLSCredsBox *credsbox;
    gnutls_session_t handle;
    char *hostname;
    char *authzid;
    bool handshakeComplete;
    QCryptoTLSSessionWriteFunc writeFunc;
    QCryptoTLSSessionReadFunc readFunc;
    void *opaque;
    char *peername;

    /*
     * Allow concurrent reads and writes, so track
     * errors separately
     */
    Error *rerr;
    Error *werr;

    /*
     * Used to protect against broken GNUTLS thread safety
     * https://gitlab.com/gnutls/gnutls/-/issues/1717
     */
    bool requireThreadSafety;
    bool lockEnabled;
    QemuMutex lock;
};


void
qcrypto_tls_session_free(QCryptoTLSSession *session)
{
    if (!session) {
        return;
    }

    error_free(session->rerr);
    error_free(session->werr);

    gnutls_deinit(session->handle);
    g_free(session->hostname);
    g_free(session->peername);
    g_free(session->authzid);
    qcrypto_tls_creds_box_unref(session->credsbox);
    object_unref(OBJECT(session->creds));
    qemu_mutex_destroy(&session->lock);
    g_free(session);
}


static ssize_t
qcrypto_tls_session_push(void *opaque, const void *buf, size_t len)
{
    QCryptoTLSSession *session = opaque;
    ssize_t ret;

    if (!session->writeFunc) {
        errno = EIO;
        return -1;
    };

    if (session->lockEnabled) {
        qemu_mutex_unlock(&session->lock);
    }

    error_free(session->werr);
    session->werr = NULL;

    ret = session->writeFunc(buf, len, session->opaque, &session->werr);

    if (session->lockEnabled) {
        qemu_mutex_lock(&session->lock);
    }

    if (ret == QCRYPTO_TLS_SESSION_ERR_BLOCK) {
        errno = EAGAIN;
        return -1;
    } else if (ret < 0) {
        errno = EIO;
        return -1;
    } else {
        return ret;
    }
}


static ssize_t
qcrypto_tls_session_pull(void *opaque, void *buf, size_t len)
{
    QCryptoTLSSession *session = opaque;
    ssize_t ret;

    if (!session->readFunc) {
        errno = EIO;
        return -1;
    };

    error_free(session->rerr);
    session->rerr = NULL;

    if (session->lockEnabled) {
        qemu_mutex_unlock(&session->lock);
    }

    ret = session->readFunc(buf, len, session->opaque, &session->rerr);

    if (session->lockEnabled) {
        qemu_mutex_lock(&session->lock);
    }

    if (ret == QCRYPTO_TLS_SESSION_ERR_BLOCK) {
        errno = EAGAIN;
        return -1;
    } else if (ret < 0) {
        errno = EIO;
        return -1;
    } else {
        return ret;
    }
}

static int
qcrypto_tls_session_check_certificate(QCryptoTLSSession *session,
                                      Error **errp)
{
    int ret;
    unsigned int status;
    const gnutls_datum_t *certs;
    unsigned int nCerts, i;
    time_t now;
    gnutls_x509_crt_t cert = NULL;
    Error *err = NULL;

    now = time(NULL);
    if (now == ((time_t)-1)) {
        error_setg_errno(errp, errno, "Cannot get current time");
        return -1;
    }

    ret = gnutls_certificate_verify_peers2(session->handle, &status);
    if (ret < 0) {
        error_setg(errp, "Verify failed: %s", gnutls_strerror(ret));
        return -1;
    }

    if (status != 0) {
        const char *reason = "Invalid certificate";

        if (status & GNUTLS_CERT_INVALID) {
            reason = "The certificate is not trusted";
        }

        if (status & GNUTLS_CERT_SIGNER_NOT_FOUND) {
            reason = "The certificate hasn't got a known issuer";
        }

        if (status & GNUTLS_CERT_REVOKED) {
            reason = "The certificate has been revoked";
        }

        if (status & GNUTLS_CERT_INSECURE_ALGORITHM) {
            reason = "The certificate uses an insecure algorithm";
        }

        error_setg(errp, "%s", reason);
        return -1;
    }

    certs = gnutls_certificate_get_peers(session->handle, &nCerts);
    if (!certs) {
        error_setg(errp, "No certificate peers");
        return -1;
    }

    for (i = 0; i < nCerts; i++) {
        ret = gnutls_x509_crt_init(&cert);
        if (ret < 0) {
            error_setg(errp, "Cannot initialize certificate: %s",
                       gnutls_strerror(ret));
            return -1;
        }

        ret = gnutls_x509_crt_import(cert, &certs[i], GNUTLS_X509_FMT_DER);
        if (ret < 0) {
            error_setg(errp, "Cannot import certificate: %s",
                       gnutls_strerror(ret));
            goto error;
        }

        if (gnutls_x509_crt_get_expiration_time(cert) < now) {
            error_setg(errp, "The certificate has expired");
            goto error;
        }

        if (gnutls_x509_crt_get_activation_time(cert) > now) {
            error_setg(errp, "The certificate is not yet activated");
            goto error;
        }

        if (gnutls_x509_crt_get_activation_time(cert) > now) {
            error_setg(errp, "The certificate is not yet activated");
            goto error;
        }

        if (i == 0) {
            gnutls_datum_t dname = {};
            ret = gnutls_x509_crt_get_dn2(cert, &dname);
            if (ret < 0) {
                error_setg(errp, "Cannot get client distinguished name: %s",
                           gnutls_strerror(ret));
                goto error;
            }
            session->peername = (char *)g_steal_pointer(&dname.data);
            trace_qcrypto_tls_session_check_x509_dn(session, session->peername);
            if (session->authzid) {
                bool allow;

                allow = qauthz_is_allowed_by_id(session->authzid,
                                                session->peername, &err);
                if (err) {
                    error_propagate(errp, err);
                    goto error;
                }
                if (!allow) {
                    error_setg(errp, "TLS x509 authz check for %s is denied",
                               session->peername);
                    goto error;
                }
            }
            if (session->hostname) {
                if (!gnutls_x509_crt_check_hostname(cert, session->hostname)) {
                    error_setg(errp,
                               "Certificate does not match the hostname %s",
                               session->hostname);
                    goto error;
                }
            } else {
                if (session->creds->endpoint ==
                    QCRYPTO_TLS_CREDS_ENDPOINT_CLIENT) {
                    error_setg(errp, "No hostname for certificate validation");
                    goto error;
                }
            }
        }

        gnutls_x509_crt_deinit(cert);
    }

    return 0;

 error:
    gnutls_x509_crt_deinit(cert);
    return -1;
}




ssize_t
qcrypto_tls_session_write(QCryptoTLSSession *session,
                          const char *buf,
                          size_t len,
                          Error **errp)
{
    ssize_t ret;

    if (session->lockEnabled) {
        qemu_mutex_lock(&session->lock);
    }

    ret = gnutls_record_send(session->handle, buf, len);

    if (session->lockEnabled) {
        qemu_mutex_unlock(&session->lock);
    }

    if (ret < 0) {
        if (ret == GNUTLS_E_AGAIN) {
            return QCRYPTO_TLS_SESSION_ERR_BLOCK;
        } else {
            if (session->werr) {
                error_propagate(errp, session->werr);
                session->werr = NULL;
            } else {
                error_setg(errp,
                           "Cannot write to TLS channel: %s",
                           gnutls_strerror(ret));
            }
            return -1;
        }
    }

    return ret;
}


ssize_t
qcrypto_tls_session_read(QCryptoTLSSession *session,
                         char *buf,
                         size_t len,
                         Error **errp)
{
    ssize_t ret;

    if (session->lockEnabled) {
        qemu_mutex_lock(&session->lock);
    }

    ret = gnutls_record_recv(session->handle, buf, len);

    if (session->lockEnabled) {
        qemu_mutex_unlock(&session->lock);
    }

    if (ret < 0) {
        if (ret == GNUTLS_E_AGAIN) {
            return QCRYPTO_TLS_SESSION_ERR_BLOCK;
        } else {
            if (session->rerr) {
                error_propagate(errp, session->rerr);
                session->rerr = NULL;
            } else {
                error_setg(errp,
                           "Cannot read from TLS channel: %s",
                           gnutls_strerror(ret));
            }
            if (ret == GNUTLS_E_PREMATURE_TERMINATION) {
                return QCRYPTO_TLS_SESSION_PREMATURE_TERMINATION;
            } else {
                return -1;
            }
        }
    }

    return ret;
}


size_t
qcrypto_tls_session_check_pending(QCryptoTLSSession *session)
{
    return gnutls_record_check_pending(session->handle);
}





#else /* ! CONFIG_GNUTLS */


void
qcrypto_tls_session_free(QCryptoTLSSession *sess G_GNUC_UNUSED)
{
}




ssize_t
qcrypto_tls_session_write(QCryptoTLSSession *sess,
                          const char *buf,
                          size_t len,
                          Error **errp)
{
    error_setg(errp, "TLS requires GNUTLS support");
    return -1;
}


ssize_t
qcrypto_tls_session_read(QCryptoTLSSession *sess,
                         char *buf,
                         size_t len,
                         Error **errp)
{
    error_setg(errp, "TLS requires GNUTLS support");
    return -1;
}


size_t
qcrypto_tls_session_check_pending(QCryptoTLSSession *session)
{
    return 0;
}





#endif

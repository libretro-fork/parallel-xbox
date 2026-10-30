/*
 *  IP checksumming functions.
 *  (c) 2008 Gerd Hoffmann <kraxel@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; under version 2 or later of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "net/checksum.h"
#include "net/eth.h"

uint32_t net_checksum_add_cont(int len, uint8_t *buf, int seq)
{
    uint32_t sum1 = 0, sum2 = 0;
    int i;

    for (i = 0; i < len - 1; i += 2) {
        sum1 += (uint32_t)buf[i];
        sum2 += (uint32_t)buf[i + 1];
    }
    if (i < len) {
        sum1 += (uint32_t)buf[i];
    }

    if (seq & 1) {
        return sum1 + (sum2 << 8);
    } else {
        return sum2 + (sum1 << 8);
    }
}

uint32_t
net_checksum_add_iov(const struct iovec *iov, const unsigned int iov_cnt,
                     uint32_t iov_off, uint32_t size, uint32_t csum_offset)
{
    size_t iovec_off;
    unsigned int i;
    uint32_t res = 0;

    iovec_off = 0;
    for (i = 0; i < iov_cnt && size; i++) {
        if (iov_off < (iovec_off + iov[i].iov_len)) {
            size_t len = MIN((iovec_off + iov[i].iov_len) - iov_off , size);
            void *chunk_buf = iov[i].iov_base + (iov_off - iovec_off);

            res += net_checksum_add_cont(len, chunk_buf, csum_offset);
            csum_offset += len;

            iov_off += len;
            size -= len;
        }
        iovec_off += iov[i].iov_len;
    }
    return res;
}

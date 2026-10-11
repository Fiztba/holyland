/* libc.a member puts.o (newlib libc/stdio/puts.c) (KFIV: not in ICO) */
#include <reent.h>
#include <stdio.h>
#include <string.h>
#include <libc_internal.h>

#define EOF (-1)

struct __siov {
    const char *iov_base; /* 0x0 */
    int iov_len;          /* 0x4 */
};

struct __suio {
    struct __siov *uio_iov; /* 0x0 */
    int uio_iovcnt;         /* 0x4 */
    int uio_resid;          /* 0x8 */
};

int _puts_r(Reent *ptr, const char *s)
{
    int result;
    unsigned int c = strlen(s);
    struct __suio uio;
    struct __siov iov[2];

    iov[0].iov_base = s;
    iov[0].iov_len = c;
    iov[1].iov_base = "\n";
    iov[1].iov_len = 1;
    uio.uio_resid = c + 1;
    uio.uio_iov = &iov[0];
    uio.uio_iovcnt = 2;
    result = __sfvwrite(ptr->out, &uio) ? EOF : '\n';
    return result;
}

int puts(const char *s)
{
    return _puts_r(_impure_ptr, s);
}

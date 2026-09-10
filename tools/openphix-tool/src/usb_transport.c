#include "transport.h"

#include <libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "protocol.h"

struct usb_priv {
    libusb_context *ctx;
    libusb_device_handle *h;
    int iface;
    uint8_t bulk_in, bulk_out, int_in, int_out;
};

static int map_result(int r, int transferred, int is_read)
{
    if (r == 0)
        return transferred;
    if (r == LIBUSB_ERROR_TIMEOUT)
        return is_read ? 0 : (transferred > 0 ? transferred : LIBUSB_ERROR_TIMEOUT);
    return r;
}

static int usb_write_cmd(struct obd_transport *t, const uint8_t *data, size_t len, unsigned timeout)
{
    struct usb_priv *p = t->priv;
    int n = 0;
    int r = libusb_interrupt_transfer(p->h, p->int_out, (unsigned char *)data, (int)len, &n, timeout);
    return map_result(r, n, 0);
}

static int usb_read_cmd(struct obd_transport *t, uint8_t *buf, size_t len, unsigned timeout)
{
    struct usb_priv *p = t->priv;
    int n = 0;
    int r = libusb_interrupt_transfer(p->h, p->int_in, buf, (int)len, &n, timeout);
    return map_result(r, n, 1);
}

static int usb_write_bulk(struct obd_transport *t, const uint8_t *data, size_t len, unsigned timeout)
{
    struct usb_priv *p = t->priv;
    int n = 0;
    int r = libusb_bulk_transfer(p->h, p->bulk_out, (unsigned char *)data, (int)len, &n, timeout);
    return map_result(r, n, 0);
}

static int usb_read_bulk(struct obd_transport *t, uint8_t *buf, size_t len, unsigned timeout)
{
    struct usb_priv *p = t->priv;
    int n = 0;
    int r = libusb_bulk_transfer(p->h, p->bulk_in, buf, (int)len, &n, timeout);
    return map_result(r, n, 1);
}

static void usb_close(struct obd_transport *t)
{
    struct usb_priv *p = t->priv;
    if (p) {
        if (p->h) {
            libusb_release_interface(p->h, p->iface);
            libusb_close(p->h);
        }
        if (p->ctx)
            libusb_exit(p->ctx);
        free(p);
    }
    free(t);
}

static const struct obd_transport_ops usb_ops = {
    usb_write_cmd, usb_read_cmd, usb_write_bulk, usb_read_bulk, usb_close,
};

static int matches(const struct libusb_device_descriptor *d, int vid, int pid)
{
    if (vid >= 0 && pid >= 0)
        return d->idVendor == vid && d->idProduct == pid;
    return strcmp(obd_hardware_name(d->idVendor, d->idProduct), "unknown") != 0;
}

static void seterr(char *err, size_t errlen, const char *fmt, const char *detail)
{
    if (err && errlen)
        snprintf(err, errlen, fmt, detail ? detail : "");
}

int obd_usb_list(int vid, int pid, char *err, size_t errlen)
{
    libusb_context *ctx = NULL;
    libusb_device **list = NULL;
    ssize_t n;
    int count = 0, r;

    r = libusb_init(&ctx);
    if (r < 0) {
        seterr(err, errlen, "libusb_init failed: %s", libusb_strerror(r));
        return r;
    }
    n = libusb_get_device_list(ctx, &list);
    if (n < 0) {
        seterr(err, errlen, "cannot list USB devices: %s", libusb_strerror((int)n));
        libusb_exit(ctx);
        return (int)n;
    }
    for (ssize_t i = 0; i < n; i++) {
        struct libusb_device_descriptor d;
        if (libusb_get_device_descriptor(list[i], &d) != 0)
            continue;
        if (!matches(&d, vid, pid))
            continue;
        printf("%04x:%04x  %s  (bus %d, address %d)\n", d.idVendor, d.idProduct,
               obd_hardware_name(d.idVendor, d.idProduct), libusb_get_bus_number(list[i]),
               libusb_get_device_address(list[i]));
        count++;
    }
    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
    return count;
}

static int find_endpoints(libusb_device *dev, struct usb_priv *p, char *err, size_t errlen)
{
    struct libusb_config_descriptor *cfg;
    int r = libusb_get_active_config_descriptor(dev, &cfg);
    if (r < 0)
        r = libusb_get_config_descriptor(dev, 0, &cfg);
    if (r < 0) {
        seterr(err, errlen, "cannot read configuration descriptor: %s", libusb_strerror(r));
        return r;
    }
    if (cfg->bNumInterfaces < 1 || cfg->interface[0].num_altsetting < 1) {
        libusb_free_config_descriptor(cfg);
        seterr(err, errlen, "device has no interface%s", NULL);
        return LIBUSB_ERROR_NOT_FOUND;
    }
    {
        const struct libusb_interface_descriptor *alt = &cfg->interface[0].altsetting[0];
        int have = 0;
        p->iface = alt->bInterfaceNumber;
        for (int i = 0; i < alt->bNumEndpoints; i++) {
            const struct libusb_endpoint_descriptor *ep = &alt->endpoint[i];
            int in = (ep->bEndpointAddress & LIBUSB_ENDPOINT_IN) != 0;
            int type = ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK;
            if (type == LIBUSB_TRANSFER_TYPE_BULK) {
                if (in) { p->bulk_in = ep->bEndpointAddress; have |= 1; }
                else    { p->bulk_out = ep->bEndpointAddress; have |= 2; }
            } else if (type == LIBUSB_TRANSFER_TYPE_INTERRUPT) {
                if (in) { p->int_in = ep->bEndpointAddress; have |= 4; }
                else    { p->int_out = ep->bEndpointAddress; have |= 8; }
            }
        }
        libusb_free_config_descriptor(cfg);
        if (have != 15) {
            seterr(err, errlen, "USB interface lacks one of bulk IN/OUT, interrupt IN/OUT%s", NULL);
            return LIBUSB_ERROR_NOT_FOUND;
        }
    }
    return 0;
}

struct obd_transport *obd_usb_open(int vid, int pid, char *err, size_t errlen)
{
    struct obd_transport *t = NULL;
    struct usb_priv *p = NULL;
    libusb_device **list = NULL;
    libusb_device *found = NULL;
    struct libusb_device_descriptor fd;
    ssize_t n;
    int r;

    memset(&fd, 0, sizeof fd);
    p = calloc(1, sizeof(*p));
    t = calloc(1, sizeof(*t));
    if (!p || !t) {
        free(p);
        free(t);
        seterr(err, errlen, "out of memory%s", NULL);
        return NULL;
    }
    t->ops = &usb_ops;
    t->priv = p;

    r = libusb_init(&p->ctx);
    if (r < 0) {
        seterr(err, errlen, "libusb_init failed: %s", libusb_strerror(r));
        goto fail;
    }
    n = libusb_get_device_list(p->ctx, &list);
    if (n < 0) {
        seterr(err, errlen, "cannot list USB devices: %s", libusb_strerror((int)n));
        goto fail;
    }
    for (ssize_t i = 0; i < n && !found; i++) {
        struct libusb_device_descriptor d;
        if (libusb_get_device_descriptor(list[i], &d) == 0 && matches(&d, vid, pid)) {
            found = list[i];
            fd = d;
        }
    }
    if (!found) {
        libusb_free_device_list(list, 1);
        seterr(err, errlen, "no scan tool found. Is it plugged in via USB, and do you have "
                            "permission to access it (udev rule on Linux)?%s", NULL);
        goto fail;
    }
    t->vid = fd.idVendor;
    t->pid = fd.idProduct;
    r = libusb_open(found, &p->h);
    if (r < 0) {
        libusb_free_device_list(list, 1);
        seterr(err, errlen, "cannot open device: %s", libusb_strerror(r));
        goto fail;
    }
    r = find_endpoints(found, p, err, errlen);
    libusb_free_device_list(list, 1);
    if (r < 0)
        goto fail;
    libusb_set_auto_detach_kernel_driver(p->h, 1);
    {
        int cur = 0;
        if (libusb_get_configuration(p->h, &cur) == 0 && cur == 0)
            libusb_set_configuration(p->h, 1);
    }
    r = libusb_claim_interface(p->h, p->iface);
    if (r < 0) {
        seterr(err, errlen, "cannot claim USB interface: %s", libusb_strerror(r));
        goto fail;
    }
    return t;

fail:
    usb_close(t);
    return NULL;
}

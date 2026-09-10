/* Byte pipes to the scan tool: real (libusb) or simulated. */
#ifndef OPENPHIX_TRANSPORT_H
#define OPENPHIX_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

struct obd_transport;

struct obd_transport_ops {
    /* All return the number of bytes transferred, 0 on timeout (reads only),
     * or a negative value on error. */
    int (*write_cmd)(struct obd_transport *t, const uint8_t *data, size_t len, unsigned timeout_ms);
    int (*read_cmd)(struct obd_transport *t, uint8_t *buf, size_t len, unsigned timeout_ms);
    int (*write_bulk)(struct obd_transport *t, const uint8_t *data, size_t len, unsigned timeout_ms);
    int (*read_bulk)(struct obd_transport *t, uint8_t *buf, size_t len, unsigned timeout_ms);
    void (*close)(struct obd_transport *t);
};

struct obd_transport {
    const struct obd_transport_ops *ops;
    uint16_t vid;
    uint16_t pid;
    void *priv;
};

static inline void obd_transport_close(struct obd_transport *t)
{
    if (t && t->ops && t->ops->close)
        t->ops->close(t);
}

/* libusb backed transport (usb_transport.c). vid/pid < 0: any known tool. */
struct obd_transport *obd_usb_open(int vid, int pid, char *err, size_t errlen);
/* Print connected tools. Returns the count, or a negative libusb error. */
int obd_usb_list(int vid, int pid, char *err, size_t errlen);

#endif

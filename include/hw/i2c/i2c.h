#ifndef QEMU_I2C_H
#define QEMU_I2C_H

#include "hw/qdev-core.h"
#include "qom/object.h"

/* The QEMU I2C implementation only supports simple transfers that complete
   immediately.  It does not support slave devices that need to be able to
   defer their response (eg. CPU slave interfaces where the data is supplied
   by the device driver in response to an interrupt).  */

enum i2c_event {
    I2C_START_RECV,
    I2C_START_SEND,
    I2C_START_SEND_ASYNC,
    I2C_FINISH,
    I2C_NACK /* Masker NACKed a receive byte.  */
};

typedef struct I2CNodeList I2CNodeList;

#define TYPE_I2C_SLAVE "i2c-slave"
OBJECT_DECLARE_TYPE(I2CSlave, I2CSlaveClass,
                    I2C_SLAVE)

struct I2CSlaveClass {
    DeviceClass parent_class;

    /* Master to slave. Returns non-zero for a NAK, 0 for success. */
    int (*send)(I2CSlave *s, uint8_t data);

    /* Master to slave (asynchronous). Receiving slave must call i2c_ack(). */
    void (*send_async)(I2CSlave *s, uint8_t data);

    /*
     * Slave to master.  This cannot fail, the device should always
     * return something here.
     */
    uint8_t (*recv)(I2CSlave *s);

    /*
     * Notify the slave of a bus state change.  For start event,
     * returns non-zero to NAK an operation.  For other events the
     * return code is not used and should be zero.
     */
    int (*event)(I2CSlave *s, enum i2c_event event);

    /*
     * Check if this device matches the address provided.  Returns bool of
     * true if it matches (or broadcast), and updates the device list, false
     * otherwise.
     *
     * If broadcast is true, match should add the device and return true.
     */
    bool (*match_and_add)(I2CSlave *candidate, uint8_t address, bool broadcast,
                          I2CNodeList *current_devs);
};

struct I2CSlave {
    DeviceState qdev;

    /* Remaining fields for internal use by the I2C code.  */
    uint8_t address;
};

#define TYPE_I2C_BUS "i2c-bus"
OBJECT_DECLARE_SIMPLE_TYPE(I2CBus, I2C_BUS)

typedef struct I2CNode I2CNode;

struct I2CNode {
    I2CSlave *elt;
    QLIST_ENTRY(I2CNode) next;
};

typedef struct I2CPendingMaster I2CPendingMaster;

struct I2CPendingMaster {
    QEMUBH *bh;
    QSIMPLEQ_ENTRY(I2CPendingMaster) entry;
};

typedef QLIST_HEAD(I2CNodeList, I2CNode) I2CNodeList;
typedef QSIMPLEQ_HEAD(I2CPendingMasters, I2CPendingMaster) I2CPendingMasters;

struct I2CBus {
    BusState qbus;
    I2CNodeList current_devs;
    I2CPendingMasters pending_masters;
    uint8_t saved_address;
    bool broadcast;

    /* Set from slave currently mastering the bus. */
    QEMUBH *bh;
};

I2CBus *i2c_init_bus(DeviceState *parent, const char *name);

/**
 * i2c_start_transfer: start a transfer on an I2C bus.
 *
 * @bus: #I2CBus to be used
 * @address: address of the slave
 * @is_recv: indicates the transfer direction
 *
 * When @is_recv is a known boolean constant, use the
 * i2c_start_recv() or i2c_start_send() helper instead.
 *
 * Returns: 0 on success, -1 on error
 */
int i2c_start_transfer(I2CBus *bus, uint8_t address, bool is_recv);

/**
 * i2c_start_recv: start a 'receive' transfer on an I2C bus.
 *
 * @bus: #I2CBus to be used
 * @address: address of the slave
 *
 * Returns: 0 on success, -1 on error
 */
int i2c_start_recv(I2CBus *bus, uint8_t address);

/**
 * i2c_start_send: start a 'send' transfer on an I2C bus.
 *
 * @bus: #I2CBus to be used
 * @address: address of the slave
 *
 * Returns: 0 on success, -1 on error
 */
int i2c_start_send(I2CBus *bus, uint8_t address);

void i2c_end_transfer(I2CBus *bus);
void i2c_nack(I2CBus *bus);
int i2c_send(I2CBus *bus, uint8_t data);
uint8_t i2c_recv(I2CBus *bus);
bool i2c_scan_bus(I2CBus *bus, uint8_t address, bool broadcast,
                  I2CNodeList *current_devs);

extern const VMStateDescription vmstate_i2c_slave;

#define VMSTATE_I2C_SLAVE(_field, _state) {                          \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(I2CSlave),                                  \
    .vmsd       = &vmstate_i2c_slave,                                \
    .flags      = VMS_STRUCT,                                        \
    .offset     = vmstate_offset_value(_state, _field, I2CSlave),    \
}

#endif

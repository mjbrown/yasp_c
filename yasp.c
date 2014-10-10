/*  Code for processing command messages
*/
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include "yasp.h"
#include "serial_HAL.h"

static command_callback command_callbacks[REGISTRY_LENGTH];
static uint8_t commands[REGISTRY_LENGTH];
static uint8_t registered_commands = 0;

void register_yasp_command(command_callback callback, uint8_t command) {
    commands[registered_commands] = command;
    command_callbacks[registered_commands] = callback;
    registered_commands += 1;
}

#define SYNCH_BYTE  0xFF

/* Packet Structure */
#define SYNCH1_POS      0
#define SYNCH2_POS      1
#define START_CHECKSUM  2
#define LENGTH_POS      2  // Synch is not included in command length or checksum
#define COMMAND_POS     4

/* Return Codes */
#define RET_CMD_INCOMPLETE  -1
#define RET_CMD_NOSYNCH     -2
#define RET_CMD_CORRUPT     -3
#define RET_CMD_FAILED      -4

int16_t process_yasp(uint8_t *buffer, uint16_t length)
{
    uint16_t cmd_len;
    uint16_t i;
    uint8_t checksum = 0;
    if (length < 6)
        return RET_CMD_INCOMPLETE;
    if ((buffer[SYNCH1_POS] & buffer[SYNCH2_POS] & SYNCH_BYTE) != SYNCH_BYTE)
        return RET_CMD_NOSYNCH;
    cmd_len = (((uint16_t) buffer[LENGTH_POS]) << 8) + ((uint16_t)buffer[LENGTH_POS+1]);
    if (cmd_len > (length - START_CHECKSUM)) // Synch not included in command length
    {
        return RET_CMD_INCOMPLETE;
    }
    for (i = START_CHECKSUM; i < cmd_len + START_CHECKSUM; i++)
    {
        checksum += buffer[i];
    }
    //printf("Actual: %02x, Checksum: %02x\n", buffer[cmd_len + START_CHECKSUM], checksum);
    if (checksum != buffer[cmd_len + START_CHECKSUM])
    {
        return RET_CMD_CORRUPT;
    }
    for (i = 0; i < registered_commands; i++)
    {
        if (commands[i] == buffer[COMMAND_POS]) {
            command_callbacks[i](buffer + COMMAND_POS + 1, cmd_len);
            break;
        }
    }
    if (i == registered_commands) {
        return RET_CMD_FAILED;
    }
    return (START_CHECKSUM + cmd_len + 1);
}

void send_yasp_ack(uint8_t cmd, uint8_t * payload, uint16_t length)
{
    int i;
    uint8_t out_buffer[2+2+1];
    uint8_t checksum = 0x00;
    out_buffer[SYNCH1_POS] = 0xFF;
    out_buffer[SYNCH2_POS] = 0xFF;
    out_buffer[LENGTH_POS] = (uint8_t)((length + 3) >> 8);
    out_buffer[LENGTH_POS+1] = (uint8_t)(length+3);
    out_buffer[COMMAND_POS] = 0x80 | cmd;
    serial_tx(out_buffer, 5);
    checksum += out_buffer[2] + out_buffer[3] + out_buffer[4];
    if (length > 0)
    {
        for (i = 0; i < length; i++)
        {
            checksum += payload[i];
        }
        serial_tx(payload, length);
    }
    serial_tx(&checksum, 1);

}

uint8_t ack_buffer[16];

uint16_t yasp_rx(uint8_t *cmd_buffer, uint16_t buffer_len)
{
    uint16_t i = 1;
    int16_t return_code = 0;
    uint16_t handled;
    return_code = process_yasp(cmd_buffer, buffer_len);
    //sprintf(ack_buffer, "RX(%d,%d):%02x%02x%02x%02x%02x", return_code, buffer_len, cmd_buffer[3], cmd_buffer[4], cmd_buffer[5], cmd_buffer[6], cmd_buffer[7]);
    //serial_tx(ack_buffer, 30);
    if (return_code > 0)
    {
        handled = return_code;
    }
    else if (return_code == RET_CMD_NOSYNCH)
    {
        while ((cmd_buffer[i++] != SYNCH_BYTE) && (i < buffer_len));
        handled = i;
    }
    else if (return_code == RET_CMD_CORRUPT)
    {
        sprintf(ack_buffer, "Corrupt!");
        send_yasp_ack(0xFF, ack_buffer, strlen(ack_buffer));
        handled = buffer_len;
    }
    else if (return_code == RET_CMD_FAILED)
    {
        sprintf(ack_buffer, "Failed!");
        send_yasp_ack(0xFF, ack_buffer, strlen(ack_buffer));
        handled = buffer_len;
    }
    else
    {
        handled = buffer_len;
    }
    return handled;
}

void yasp_init()
{
    setSerialRxHandler(yasp_rx);
}

#ifndef LIBDRIVERS_MODBUS_H
#define LIBDRIVERS_MODBUS_H

#include "payload.h"

#include <bitset>
#include <cstdint>
#include <functional>
#include <memory>

/* The size of the MODBUS PDU is limited by the size constraint inherited from
 * the first MODBUS implementation on Serial Line network (max. RS485 ADU = 256
 * bytes). Therefore, MODBUS PDU for serial line communication = 256 - Server
 * address (1 byte) - CRC (2 bytes) = 253 bytes.
 */
#define MODBUS_MAX_PDU_LENGTH 253

/* Consequently:
 * - RTU MODBUS ADU = 253 bytes + Server address (1 byte) + CRC (2 bytes) = 256
 *   bytes.
 * - TCP MODBUS ADU = 253 bytes + MBAP (7 bytes) = 260 bytes.
 * so the maximum of both backend in 260 bytes. This size can used to allocate
 * an array of bytes to store responses and it will be compatible with the two
 * backends.
 */
#define MODBUS_MAX_ADU_LENGTH 260

namespace whisper
{
    struct modbus
    {
        // function codes
        enum fcode : unsigned char
        {
            // bit access
            READ_COILS                 = 0x01,
            READ_DISCREATE_INPUTS      = 0x02,
            WRITE_COIL                 = 0x05,
            WRITE_COILS                = 0x0F,
            // 16-bit access
            READ_HOLDING_REGISTERS     = 0x03,
            READ_INPUT_REGISTERS       = 0x04,
            WRITE_HOLDING_REGISTER     = 0x06,
            WRITE_HOLDING_REGISTERS    = 0x10,
            MASK_WRITE_REGISTER        = 0x16,
            READ_AND_WRITE_REGISTERS   = 0x17,
            READ_FIFO_QUEUE            = 0x18,
            // file record access
            READ_FILE_RECORD           = 0x14,
            WRITE_FILE_RECORD          = 0x15,
            // diagnostics
            READ_EXCEPTION_STATUS      = 0x07,
            DIAGNOSTIC                 = 0x08,
            GET_COM_EVENT_COUNTER      = 0x0B,
            GET_COM_EVENT_LOG          = 0x0C,
            REPORT_SERVER_ID           = 0x11,
            READ_DEVICE_IDENTIFICATION = 0x2B,
            // error
            ERROR_MASK                 = 0x80,
        };

        // exception codes
        enum class ecode : unsigned char
        {
            ILLEGAL_FUNCTION                        = 0x01,
            ILLEGAL_DATA_ADDRESS                    = 0x02,
            ILLEGAL_DATA_VALUE                      = 0x03,
            SERVER_DEVICE_FAILURE                   = 0x04,
            ACKNOWLEDGE                             = 0x05,
            SERVER_DEVICE_BUSY                      = 0x06,
            MEMORY_PARITY_ERROR                     = 0x08,
            GATEWAY_PATH_UNAVAILABLE                = 0x0A,
            GATEWAY_TARGET_DEVICE_FAILED_TO_RESPOND = 0x0B,
        };

        // id
        uint8_t id{ 0x01 };

        // data model
        std::bitset<64> discretes_input{};        // read-only
        std::bitset<64> coils[1]{};               // read-write
        uint16_t        input_registers[256]{};   // read-only
        uint16_t        holding_registers[256]{}; // read-write

        //
        void rx(const void *buffer, size_t size);

        void handle(payload& request);

        std::function<void(const payload&)> tx = [](auto) {};
    };
} // namespace whisper

#endif //! LIBDRIVERS_MODBUS_H

#include "modbus.h"

#include "crc.h"
#include "payload.h"

namespace whisper
{
    void modbus::rx(const void *buffer, const size_t size)
    {
        const auto data = static_cast<const uint8_t *>(buffer);

        // check id
        if (data[0] != id || size < 3) return;

        // checksum
        if (*reinterpret_cast<const uint16_t *>(data + size - 2) != crc_modbus(data, size - 2)) return;

        //
        payload request{ data + 1, size - 3 };

        handle(request);
    }

    void modbus::handle(payload& request)
    {
        switch (const auto code = request.read<fcode>()) {
        // Request
        //  function code           : 1 - 0x03
        //  starting address        : 2
        //  quantity of registers   : 2
        // Response
        //  function code           : 1 - 0x03
        //  byte count              : 1 - 2N
        //  register value          : 2N
        // Error
        //  error code              : 1 - 0x83
        //  exception code          : 1 - 01, 02, 03, or 04
        case fcode::READ_HOLDING_REGISTERS: {
            payload response{};
            response.write(id);

            const auto starting_address      = request.read<uint16_t, endianness_t::big>();
            const auto quantity_of_registers = request.read<uint16_t, endianness_t::big>();

            // 0x0001 <= Quantity of Registers <= 0x007D
            if (quantity_of_registers > 0x007D || quantity_of_registers >= sizeof(holding_registers) ||
                quantity_of_registers < 0x0001) {
                response.write(fcode::ERROR_MASK | code);
                response.write(ecode::ILLEGAL_DATA_VALUE);
                response.write(crc_modbus(response.data(), response.size()));
                tx(response);
                break;
            }

            // Starting Address == OK AND Starting Address + Quantity of Registers == OK
            if (starting_address + quantity_of_registers >= sizeof(holding_registers)) {
                response.write(fcode::ERROR_MASK | code);
                response.write(ecode::ILLEGAL_DATA_ADDRESS);
                response.write(crc_modbus(response.data(), response.size()));
                tx(response);
                break;
            }

            // response
            {
                // function code
                response.write(code);
                // byte count
                response.write(static_cast<uint8_t>(quantity_of_registers * 2));
                // registers
                for (size_t i = 0; i < quantity_of_registers; ++i) {
                    response.write<uint16_t, endianness_t::big>(holding_registers[starting_address + i]);
                }
                // checksum
                response.write(crc_modbus(response.data(), response.size()));
            }

            tx(response);
            break;
        }

        // Request
        //  function code           : 1 - 0x04
        //  starting address        : 2
        //  quantity of registers   : 2
        // Response
        //  function code           : 1 - 0x04
        //  byte count              : 1 - 2N
        //  input registers         : 2N
        // Error
        //  error code              : 1 - 0x84
        //  exception code          : 1 - 01, 02, 03, or 04
        case fcode::READ_INPUT_REGISTERS: {
            payload response{};
            response.write(id);

            const auto starting_address      = request.read<uint16_t, endianness_t::big>();
            const auto quantity_of_registers = request.read<uint16_t, endianness_t::big>();

            // 0x0001 <= Quantity of Registers <= 0x007D
            if (quantity_of_registers > 0x007D || quantity_of_registers < 0x0001) {
                response.write(fcode::ERROR_MASK | code);
                response.write(ecode::ILLEGAL_DATA_VALUE);
                response.write(crc_modbus(response.data(), response.size()));
                tx(response);
                break;
            }

            // Starting Address == OK AND Starting Address + Quantity of Registers == OK
            if (starting_address + quantity_of_registers >= sizeof(input_registers)) {
                response.write(fcode::ERROR_MASK | code);
                response.write(ecode::ILLEGAL_DATA_ADDRESS);
                response.write(crc_modbus(response.data(), response.size()));
                tx(response);
                break;
            }

            // response
            {
                // function code
                response.write(code);
                // bytes
                response.write(static_cast<uint8_t>(quantity_of_registers * 2));
                // registers
                for (size_t i = 0; i < quantity_of_registers; ++i) {
                    response.write<uint16_t, endianness_t::big>(input_registers[starting_address + i]);
                }
                // checksum
                response.write(crc_modbus(response.data(), response.size()));
            }

            tx(response);
            break;
        }
        // Request
        //  function code           : 1 - 0x06
        //  register address        : 2
        //  register value          : 2
        // Response
        //  function code           : 1 - 0x06
        //  register address        : 2
        //  register value          : 2
        // Error
        //  error code              : 1 - 0x86
        //  exception code          : 1 - 01, 02, 03, or 04
        case fcode::WRITE_HOLDING_REGISTER: {
            payload response{};
            response.write(id);

            const auto address = request.read<uint16_t, endianness_t::big>();
            const auto value   = request.read<uint16_t, endianness_t::big>();

            // Starting Address == OK AND Starting Address + Quantity of Registers == OK
            if (address >= sizeof(holding_registers)) {
                response.write(fcode::ERROR_MASK | code);
                response.write(ecode::ILLEGAL_DATA_ADDRESS);
                response.write(crc_modbus(response.data(), response.size()));
                tx(response);
                break;
            }

            // process
            {
                // write value
                holding_registers[address] = value;
            }

            // response
            {

                // function code
                response.write(code);
                // register address
                response.write<uint16_t, endianness_t::big>(address);
                // register value
                response.write<uint16_t, endianness_t::big>(holding_registers[address]);
                // checksum
                response.write(crc_modbus(response.data(), response.size()));
            }

            tx(response);
            break;
        }
        // Request
        //  function code           : 1 - 0x10
        //  starting address        : 2
        //  quantity of registers   : 2
        //  byte count              : 1 - 2N
        //  register value          : 2N
        // Response
        //  function code           : 1 - 0x10
        //  starting address        : 2
        //  quantity of registers   : 2 - N
        // Error
        //  error code              : 1 - 0x90
        //  exception code          : 1 - 01, 02, 03, or 04
        case fcode::WRITE_HOLDING_REGISTERS: {
            payload response{};
            response.write(id);

            const auto starting_address      = request.read<uint16_t, endianness_t::big>();
            const auto quantity_of_registers = request.read<uint16_t, endianness_t::big>();
            const auto bytes                 = request.read<uint8_t>();

            // 0x0001 <= Quantity of Registers <= 0x007B
            if (quantity_of_registers > 0x7B || quantity_of_registers >= sizeof(holding_registers) ||
                quantity_of_registers < 0x01 || bytes != quantity_of_registers * 2) {
                response.write(fcode::ERROR_MASK | code);
                response.write(ecode::ILLEGAL_DATA_VALUE);
                response.write(crc_modbus(response.data(), response.size()));
                tx(response);
                break;
            }

            // Starting Address == OK AND Starting Address + Quantity of Registers == OK
            if (starting_address + quantity_of_registers > sizeof(holding_registers)) {
                response.write(fcode::ERROR_MASK | code);
                response.write(ecode::ILLEGAL_DATA_ADDRESS);
                response.write(crc_modbus(response.data(), response.size()));
                tx(response);
                break;
            }

            // processing
            {
                // registers
                for (size_t i = 0; i < quantity_of_registers; ++i) {
                    holding_registers[starting_address + i] = request.read<uint16_t, endianness_t::big>();
                }
            }

            // response
            {
                // function code
                response.write(code);
                // starting address
                response.write<uint16_t, endianness_t::big>(starting_address);
                // quantity of registers
                response.write<uint16_t, endianness_t::big>(quantity_of_registers);
                // checksum
                response.write(crc_modbus(response.data(), response.size()));
            }

            tx(response);
            break;
        }
        // Request
        //  function code           : 1 - 0x17
        //  read starting address   : 2
        //  quantity to read        : 2 - M
        //  write starting address  : 2
        //  quantity to write       : 2 - N
        //  write byte count        : 1 - 2N
        //  write register value    : 2N
        // Response
        //  function code           : 1 - 0x17
        //  byte count              : 1 - 2M
        //  read register value     : 2M
        // Error
        //  error code              : 1 - 0x90
        //  exception code          : 1 - 01, 02, 03, or 04
        //
        // The write operation is performed before the read.
        case fcode::READ_AND_WRITE_REGISTERS: {
            payload response{};
            response.write(id);

            const auto read_starting_address  = request.read<uint16_t, endianness_t::big>();
            const auto quantity_to_read       = request.read<uint16_t, endianness_t::big>();
            const auto write_starting_address = request.read<uint16_t, endianness_t::big>();
            const auto quantity_to_write      = request.read<uint16_t, endianness_t::big>();
            const auto write_bytes            = request.read<uint8_t>();

            // 0x0001 <= Quantity of Registers <= 0x007B
            if ((quantity_to_read > 0x7D || quantity_to_read >= sizeof(holding_registers)) ||
                (quantity_to_write > 0x79 || quantity_to_write >= sizeof(holding_registers)) ||
                (quantity_to_read < 0x01 || quantity_to_write < 0x01) ||
                (write_bytes != quantity_to_write * 2)) {
                response.write(fcode::ERROR_MASK | code);
                response.write(ecode::ILLEGAL_DATA_VALUE);
                response.write(crc_modbus(response.data(), response.size()));
                tx(response);
                break;
            }

            // Starting Address == OK AND Starting Address + Quantity of Registers == OK
            if ((read_starting_address + quantity_to_read >= sizeof(holding_registers)) ||
                (write_starting_address + quantity_to_write >= sizeof(holding_registers))) {
                response.write(fcode::ERROR_MASK | code);
                response.write(ecode::ILLEGAL_DATA_ADDRESS);
                response.write(crc_modbus(response.data(), response.size()));
                tx(response);
                break;
            }

            // processing
            {
                // registers
                for (size_t i = 0; i < quantity_to_write; ++i) {
                    holding_registers[write_starting_address + i] =
                        request.read<uint16_t, endianness_t::big>();
                }
            }

            // response
            {
                // function code
                response.write(code);
                // byte count
                response.write(static_cast<uint8_t>(quantity_to_read * 2));
                // read register value
                for (size_t i = 0; i < quantity_to_read; ++i) {
                    response.write<uint16_t, endianness_t::big>(
                        holding_registers[read_starting_address + i]);
                }

                // checksum
                response.write(crc_modbus(response.data(), response.size()));
            }

            tx(response);
            break;
        }

        // unsupported
        default:
            payload response{};
            response.write<uint8_t>(id);
            response.write(fcode::ERROR_MASK | code);
            response.write(ecode::ILLEGAL_FUNCTION);
            tx(response);
            break;
        }
    }
} // namespace whisper

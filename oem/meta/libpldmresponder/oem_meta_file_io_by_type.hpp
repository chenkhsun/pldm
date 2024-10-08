#pragma once
#include <cstdint>
#include <vector>

namespace pldm::responder::oem_meta
{
/**
 *  @class FileHandler
 *
 *  Base class to handle read/write of all OEM file types
 */

using message = std::span<uint8_t>;
using configDbusPath = std::string;
constexpr auto decodeDataMaxLength = 32;

enum pldm_oem_meta_file_io_type : uint8_t
{
    POST_CODE = 0x00,
    BIOS_VERSION = 0x01,
    POWER_CONTROL = 0x02,
    APML_ALERT = 0x04,
    EVENT_LOG = 0x05,
};

class FileHandler
{
  public:
    virtual int write(const message& data) = 0;
    virtual int read(const message& data) = 0;
    virtual ~FileHandler() {}
};

} // namespace pldm::responder::oem_meta

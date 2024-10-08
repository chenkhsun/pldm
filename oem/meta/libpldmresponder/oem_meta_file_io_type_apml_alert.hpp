#pragma once

#include "common/utils.hpp"
#include "oem_meta_file_io_by_type.hpp"

namespace pldm::responder::oem_meta
{
/** @class APMLAlertHandler
 *
 *  @brief Inherits and implements FileHandler. This class is used
 *  to handle incoming sled cycle request from Hosts
 */
class APMLAlertHandler : public FileHandler
{
  public:
    APMLAlertHandler() = delete;
    APMLAlertHandler(const APMLAlertHandler&) = delete;
    APMLAlertHandler(APMLAlertHandler&&) = delete;
    APMLAlertHandler& operator=(const APMLAlertHandler&) = delete;
    APMLAlertHandler& operator=(APMLAlertHandler&&) = delete;
    ~APMLAlertHandler() = default;

    explicit APMLAlertHandler(uint8_t tid,
                              const pldm::utils::DBusHandler* dBusIntf) :
        tid(tid),
        dBusIntf(dBusIntf)
    {}

    int write(const message& data) override;
    int read(const message&) override
    {
        return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
    }

  private:
    /** @brief The requester's TID */
    uint8_t tid = 0;

    /** @brief D-Bus Interface object*/
    const pldm::utils::DBusHandler* dBusIntf;
};

} // namespace pldm::responder::oem_meta

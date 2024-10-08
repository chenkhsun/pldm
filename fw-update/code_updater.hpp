#pragma once

#include "update_manager.hpp"

#include <xyz/openbmc_project/Software/ApplyTime/server.hpp>
#include <xyz/openbmc_project/Software/Update/server.hpp>

#include <filesystem>
#include <memory>

namespace pldm::fw_update
{

//using UpdateIntf = sdbusplus::xyz::openbmc_project::Software::server::Update;
using UpdateIntf = sdbusplus::server::object_t<
    sdbusplus::server::xyz::openbmc_project::software::Update>;
using ApplyTimeIntf = sdbusplus::server::object_t<
    sdbusplus::server::xyz::openbmc_project::software::ApplyTime>;

class CodeUpdater : public UpdateIntf
{
  public:
    CodeUpdater() = delete;
    CodeUpdater(const CodeUpdater&) = delete;
    CodeUpdater(CodeUpdater&&) = delete;
    CodeUpdater& operator=(const CodeUpdater&) = delete;
    CodeUpdater& operator=(CodeUpdater&&) = delete;
    ~CodeUpdater() = default;

    CodeUpdater(sdbusplus::bus::bus& bus, const std::string& path,
                const std::shared_ptr<UpdateManager>& updateManager) :
        UpdateIntf(bus, path.c_str()),
        updateManager(updateManager)
    {}

  private:
    virtual sdbusplus::message::object_path
        startUpdate(sdbusplus::message::unix_fd image,
                    ApplyTimeIntf::RequestedApplyTimes applyTime) override;

    bool writeToFile(int imageFd, const std::string& path);

    /* @brief Convert Update Interface apply time to Apply time interface */
    //ApplyTimeIntf::RequestedApplyTimes convertApplyTimes(ApplyTimes applyTime);

    std::shared_ptr<UpdateManager> updateManager;
};

} // namespace pldm::fw_update
                              
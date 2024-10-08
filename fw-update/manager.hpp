#pragma once

#include "activation.hpp"
#include "aggregate_update_manager.hpp"
#include "common/instance_id.hpp"
#include "common/types.hpp"
#include "device_updater.hpp"
#include "inventory_manager.hpp"
#include "requester/configuration_discovery_handler.hpp"
#include "requester/handler.hpp"
#include "requester/mctp_endpoint_discovery.hpp"
#include "update_manager.hpp"

#include <unordered_map>
#include <vector>

namespace pldm
{

namespace fw_update
{

/** @class Manager
 *
 * This class handles all the aspects of the PLDM FW update specification for
 * the MCTP devices
 */
class Manager : public pldm::MctpDiscoveryHandlerIntf
{
  public:
    Manager() = delete;
    Manager(const Manager&) = delete;
    Manager(Manager&&) = delete;
    Manager& operator=(const Manager&) = delete;
    Manager& operator=(Manager&&) = delete;
    ~Manager() = default;

    /** @brief Constructor
     *
     *  @param[in] handler - PLDM request handler
     */
    explicit Manager(
        Event& event, requester::Handler<requester::Request>& handler,
        pldm::InstanceIdDb& instanceIdDb,
        pldm::ConfigurationDiscoveryHandler* configurationDiscovery) :
        updateManager(std::make_shared<AggregateUpdateManager>(
            std::make_shared<UpdateManager>(event, handler, instanceIdDb,
                                            descriptorMap, componentInfoMap))),
        inventoryMgr(event, handler, instanceIdDb, descriptorMap,
                     downstreamDescriptorMap, componentInfoMap,
                     std::static_pointer_cast<AggregateUpdateManager>(updateManager),
                     configurationDiscovery)
        {}

    /** @brief Helper function to invoke registered handlers for
     *         the added MCTP endpoints
     *
     *  @param[in] mctpInfos - information of discovered MCTP endpoints
     */
    void handleMctpEndpoints(const MctpInfos& mctpInfos)
    {
        inventoryMgr.discoverFDs(mctpInfos);
    }

    /** @brief Helper function to invoke registered handlers for
     *         the removed MCTP endpoints
     *
     *  @param[in] mctpInfos - information of removed MCTP endpoints
     */
    void handleRemovedMctpEndpoints(const MctpInfos&)
    {
        return;
    }

    /** @brief Handle PLDM request for the commands in the FW update
     *         specification
     *
     *  @param[in] eid - Remote MCTP Endpoint ID
     *  @param[in] command - PLDM command code
     *  @param[in] request - PLDM request message
     *  @param[in] requestLen - PLDM request message length
     *  @return PLDM response message
     */
    Response handleRequest(mctp_eid_t eid, Command command,
                           const pldm_msg* request, size_t reqMsgLen)
    {
        return updateManager->handleRequest(eid, command, request, reqMsgLen);
    }

  private:
    /** Descriptor information of all the discovered MCTP endpoints */
    DescriptorMap descriptorMap;

    /** Downstream descriptor information of all the discovered MCTP endpoints
     */
    DownstreamDescriptorMap downstreamDescriptorMap;

    /** Component information of all the discovered MCTP endpoints */
    ComponentInfoMap componentInfoMap;

    /** @brief PLDM update manager */
    std::shared_ptr<UpdateManagerInf> updateManager;

    /** @brief PLDM firmware inventory manager */
    InventoryManager inventoryMgr;
};

} // namespace fw_update

} // namespace pldm

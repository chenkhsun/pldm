#include "terminus.hpp"

#include "libpldm/platform.h"

#include "terminus_manager.hpp"

#include <common/utils.hpp>

#include <ranges>

namespace pldm
{
namespace platform_mc
{
/* default the max message buffer size BMC supported to 4K bytes */
#define MAX_MESSAGE_BUFFER_SIZE 4096

Terminus::Terminus(pldm_tid_t tid, uint64_t supportedTypes) :
    initialized(false), maxBufferSize(MAX_MESSAGE_BUFFER_SIZE),
    synchronyConfigurationSupported(0), pollEvent(false), tid(tid),
    supportedTypes(supportedTypes)
{
    inventoryPath = "/xyz/openbmc_project/inventory/Item/Board/PLDM_Device_" +
                    std::to_string(tid);
    try
    {
        inventoryItemBoardInft = std::make_unique<InventoryItemBoardIntf>(
            utils::DBusHandler::getBus(), inventoryPath.c_str());
    }
    catch (const sdbusplus::exception_t& e)
    {
        lg2::error(
            "Failed to create Inventory Board interface for device {PATH}",
            "PATH", inventoryPath);
        throw sdbusplus::xyz::openbmc_project::Common::Error::InvalidArgument();
    }
}

bool Terminus::doesSupportType(uint8_t type)
{
    return supportedTypes.test(type);
}

bool Terminus::doesSupportCommand(uint8_t type, uint8_t command)
{
    if (!doesSupportType(type))
    {
        return false;
    }

    try
    {
        const size_t idx = type * (PLDM_MAX_CMDS_PER_TYPE / 8) + (command / 8);
        if (idx >= supportedCmds.size())
        {
            return false;
        }

        if (supportedCmds[idx] & (1 << (command % 8)))
        {
            lg2::info(
                "PLDM type {TYPE} command {CMD} is supported by terminus {TID}",
                "TYPE", type, "CMD", command, "TID", getTid());
            return true;
        }
    }
    catch (const std::exception& e)
    {
        return false;
    }

    return false;
}

std::optional<std::string_view> Terminus::findTerminusName()
{
    auto it = std::find_if(
        entityAuxiliaryNamesTbl.begin(), entityAuxiliaryNamesTbl.end(),
        [](const std::shared_ptr<EntityAuxiliaryNames>& entityAuxiliaryNames) {
        const auto& [key, entityNames] = *entityAuxiliaryNames;
        /**
         * There is only one Overal system container entity in one terminus.
         * The entity auxiliary name PDR of that terminus with the that type of
         * containerID will include terminus name.
         **/
        return (entityAuxiliaryNames &&
                key.containerId == PLDM_PLATFORM_ENTITY_SYSTEM_CONTAINER_ID &&
                entityNames.size());
    });

    if (it != entityAuxiliaryNamesTbl.end())
    {
        const auto& [key, entityNames] = **it;
        if (!entityNames.size())
        {
            return std::nullopt;
        }
        return entityNames[0].second;
    }

    return std::nullopt;
}

void Terminus::parseTerminusPDRs()
{
    std::vector<std::shared_ptr<pldm_numeric_sensor_value_pdr>>
        numericSensorPdrs{};
    std::vector<std::shared_ptr<pldm_compact_numeric_sensor_pdr>>
        compactNumericSensorPdrs{};

    for (auto& pdr : pdrs)
    {
        auto pdrHdr = reinterpret_cast<pldm_pdr_hdr*>(pdr.data());
        switch (pdrHdr->type)
        {
            case PLDM_SENSOR_AUXILIARY_NAMES_PDR:
            {
                auto sensorAuxNames = parseSensorAuxiliaryNamesPDR(pdr);
                if (!sensorAuxNames)
                {
                    lg2::error(
                        "Failed to parse PDR with type {TYPE} handle {HANDLE}",
                        "TYPE", pdrHdr->type, "HANDLE",
                        static_cast<uint32_t>(pdrHdr->record_handle));
                    continue;
                }
                sensorAuxiliaryNamesTbl.emplace_back(std::move(sensorAuxNames));
                break;
            }
            case PLDM_NUMERIC_SENSOR_PDR:
            {
                auto parsedPdr = parseNumericSensorPDR(pdr);
                if (!parsedPdr)
                {
                    lg2::error(
                        "Failed to parse PDR with type {TYPE} handle {HANDLE}",
                        "TYPE", pdrHdr->type, "HANDLE",
                        static_cast<uint32_t>(pdrHdr->record_handle));
                    continue;
                }
                numericSensorPdrs.emplace_back(std::move(parsedPdr));
                break;
            }
            case PLDM_COMPACT_NUMERIC_SENSOR_PDR:
            {
                auto parsedPdr = parseCompactNumericSensorPDR(pdr);
                if (!parsedPdr)
                {
                    lg2::error(
                        "Failed to parse PDR with type {TYPE} handle {HANDLE}",
                        "TYPE", pdrHdr->type, "HANDLE",
                        static_cast<uint32_t>(pdrHdr->record_handle));
                    continue;
                }
                auto sensorAuxNames = parseCompactNumericSensorNames(pdr);
                if (!sensorAuxNames)
                {
                    lg2::error(
                        "Failed to parse sensor name PDR with type {TYPE} handle {HANDLE}",
                        "TYPE", pdrHdr->type, "HANDLE",
                        static_cast<uint32_t>(pdrHdr->record_handle));
                    continue;
                }
                compactNumericSensorPdrs.emplace_back(std::move(parsedPdr));
                sensorAuxiliaryNamesTbl.emplace_back(std::move(sensorAuxNames));
                break;
            }
            case PLDM_ENTITY_AUXILIARY_NAMES_PDR:
            {
                auto entityNames = parseEntityAuxiliaryNamesPDR(pdr);
                if (!entityNames)
                {
                    lg2::error(
                        "Failed to parse sensor name PDR with type {TYPE} handle {HANDLE}",
                        "TYPE", pdrHdr->type, "HANDLE",
                        static_cast<uint32_t>(pdrHdr->record_handle));
                    continue;
                }
                entityAuxiliaryNamesTbl.emplace_back(std::move(entityNames));
                break;
            }
            default:
            {
                lg2::error("Unsupported PDR with type {TYPE} handle {HANDLE}",
                           "TYPE", pdrHdr->type, "HANDLE",
                           static_cast<uint32_t>(pdrHdr->record_handle));
                break;
            }
        }
    }

    auto tName = findTerminusName();
    if (tName && !tName.value().empty())
    {
        lg2::info("Terminus {TID} has Auxiliary Name {NAME}.", "TID", tid,
                  "NAME", tName.value());
        terminusName = static_cast<std::string>(tName.value());
    }

    for (auto pdr : numericSensorPdrs)
    {
        addNumericSensor(pdr);
    }

    for (auto pdr : compactNumericSensorPdrs)
    {
        addCompactNumericSensor(pdr);
    }
}

std::shared_ptr<SensorAuxiliaryNames>
    Terminus::getSensorAuxiliaryNames(SensorId id)
{
    auto it = std::find_if(
        sensorAuxiliaryNamesTbl.begin(), sensorAuxiliaryNamesTbl.end(),
        [id](
            const std::shared_ptr<SensorAuxiliaryNames>& sensorAuxiliaryNames) {
        const auto& [sensorId, sensorCnt, sensorNames] = *sensorAuxiliaryNames;
        return sensorId == id;
    });

    if (it != sensorAuxiliaryNamesTbl.end())
    {
        return *it;
    }
    return nullptr;
};

std::shared_ptr<SensorAuxiliaryNames>
    Terminus::parseSensorAuxiliaryNamesPDR(const std::vector<uint8_t>& pdrData)
{
    constexpr uint8_t nullTerminator = 0;
    auto pdr = reinterpret_cast<const struct pldm_sensor_auxiliary_names_pdr*>(
        pdrData.data());
    const uint8_t* ptr = pdr->names;
    std::vector<AuxiliaryNames> sensorAuxNames{};
    char16_t alignedBuffer[PLDM_STR_UTF_16_MAX_LEN];
    for ([[maybe_unused]] const auto& sensor :
         std::views::iota(0, static_cast<int>(pdr->sensor_count)))
    {
        const uint8_t nameStringCount = static_cast<uint8_t>(*ptr);
        ptr += sizeof(uint8_t);
        AuxiliaryNames nameStrings{};
        for ([[maybe_unused]] const auto& count :
             std::views::iota(0, static_cast<int>(nameStringCount)))
        {
            std::string_view nameLanguageTag(
                reinterpret_cast<const char*>(ptr));
            ptr += nameLanguageTag.size() + sizeof(nullTerminator);

            int u16NameStringLen = 0;
            for (int i = 0; ptr[i] != 0 || ptr[i + 1] != 0; i += 2)
            {
                u16NameStringLen++;
            }
            /* include terminator */
            u16NameStringLen++;

            std::fill(std::begin(alignedBuffer), std::end(alignedBuffer), 0);
            if (u16NameStringLen > PLDM_STR_UTF_16_MAX_LEN)
            {
                lg2::error("Sensor name to long.");
                return nullptr;
            }
            memcpy(alignedBuffer, ptr, u16NameStringLen * sizeof(uint16_t));
            std::u16string u16NameString(alignedBuffer, u16NameStringLen);
            ptr += (u16NameString.size() + sizeof(nullTerminator)) *
                   sizeof(uint16_t);
            std::transform(u16NameString.cbegin(), u16NameString.cend(),
                           u16NameString.begin(),
                           [](uint16_t utf16) { return be16toh(utf16); });
            std::string nameString =
                std::wstring_convert<std::codecvt_utf8_utf16<char16_t>,
                                     char16_t>{}
                    .to_bytes(u16NameString);
            nameStrings.emplace_back(std::make_pair(
                nameLanguageTag, pldm::utils::trimNameForDbus(nameString)));
        }
        sensorAuxNames.emplace_back(std::move(nameStrings));
    }
    return std::make_shared<SensorAuxiliaryNames>(
        pdr->sensor_id, pdr->sensor_count, std::move(sensorAuxNames));
}

std::shared_ptr<EntityAuxiliaryNames>
    Terminus::parseEntityAuxiliaryNamesPDR(const std::vector<uint8_t>& pdrData)
{
    auto names_offset = sizeof(struct pldm_pdr_hdr) +
                        PLDM_PDR_ENTITY_AUXILIARY_NAME_PDR_MIN_LENGTH;
    auto names_size = pdrData.size() - names_offset;

    size_t decodedPdrSize = sizeof(struct pldm_entity_auxiliary_names_pdr) +
                            names_size;
    auto vPdr = std::vector<char>(decodedPdrSize);
    auto decodedPdr =
        reinterpret_cast<struct pldm_entity_auxiliary_names_pdr*>(vPdr.data());

    auto rc = decode_entity_auxiliary_names_pdr(pdrData.data(), pdrData.size(),
                                                decodedPdr, decodedPdrSize);

    if (rc)
    {
        lg2::error(
            "Failed to decode Entity Auxiliary Name PDR data, error {RC}.",
            "RC", rc);
        return nullptr;
    }

    auto vNames =
        std::vector<pldm_entity_auxiliary_name>(decodedPdr->name_string_count);
    decodedPdr->names = vNames.data();

    rc = decode_pldm_entity_auxiliary_names_pdr_index(decodedPdr);
    if (rc)
    {
        lg2::error("Failed to decode Entity Auxiliary Name, error {RC}.", "RC",
                   rc);
        return nullptr;
    }

    AuxiliaryNames nameStrings{};
    for (const auto& count :
         std::views::iota(0, static_cast<int>(decodedPdr->name_string_count)))
    {
        std::string_view nameLanguageTag =
            static_cast<std::string_view>(decodedPdr->names[count].tag);
        const size_t u16NameStringLen =
            std::char_traits<char16_t>::length(decodedPdr->names[count].name);
        std::u16string u16NameString(decodedPdr->names[count].name,
                                     u16NameStringLen);
        std::transform(u16NameString.cbegin(), u16NameString.cend(),
                       u16NameString.begin(),
                       [](uint16_t utf16) { return be16toh(utf16); });
        std::string nameString =
            std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>{}
                .to_bytes(u16NameString);
        nameStrings.emplace_back(std::make_pair(
            nameLanguageTag, pldm::utils::trimNameForDbus(nameString)));
    }

    EntityKey key{decodedPdr->container.entity_type,
                  decodedPdr->container.entity_instance_num,
                  decodedPdr->container.entity_container_id};

    return std::make_shared<EntityAuxiliaryNames>(key, nameStrings);
}

std::shared_ptr<pldm_numeric_sensor_value_pdr>
    Terminus::parseNumericSensorPDR(const std::vector<uint8_t>& pdr)
{
    const uint8_t* ptr = pdr.data();
    auto parsedPdr = std::make_shared<pldm_numeric_sensor_value_pdr>();
    auto rc = decode_numeric_sensor_pdr_data(ptr, pdr.size(), parsedPdr.get());
    if (rc)
    {
        lg2::error("Failed to decode Numeric Sensor PDR data, error {RC} ",
                   "RC", rc);
        return nullptr;
    }
    return parsedPdr;
}

void Terminus::addNumericSensor(
    const std::shared_ptr<pldm_numeric_sensor_value_pdr> pdr)
{
    uint16_t sensorId = pdr->sensor_id;
    std::string sensorName = "PLDM_Device_" + std::to_string(sensorId) + "_" +
                             std::to_string(tid);

    if (pdr->sensor_auxiliary_names_pdr)
    {
        auto sensorAuxiliaryNames = getSensorAuxiliaryNames(sensorId);
        if (sensorAuxiliaryNames)
        {
            const auto& [sensorId, sensorCnt,
                         sensorNames] = *sensorAuxiliaryNames;
            if (sensorCnt == 1)
            {
                for (const auto& [languageTag, name] : sensorNames[0])
                {
                    if (languageTag == "en")
                    {
                        sensorName = name + "_" + std::to_string(sensorId) +
                                     "_" + std::to_string(tid);
                    }
                }
            }
        }
    }

    try
    {
        auto sensor = std::make_shared<NumericSensor>(
            tid, true, pdr, sensorName, inventoryPath);
        lg2::info("Created NumericSensor {NAME}", "NAME", sensorName);
        numericSensors.emplace_back(sensor);
    }
    catch (const sdbusplus::exception_t& e)
    {
        lg2::error(
            "Failed to create NumericSensor. error - {ERROR} sensorname - {NAME}",
            "ERROR", e, "NAME", sensorName);
    }
}

std::shared_ptr<SensorAuxiliaryNames>
    Terminus::parseCompactNumericSensorNames(const std::vector<uint8_t>& sPdr)
{
    std::vector<std::vector<std::pair<NameLanguageTag, SensorName>>>
        sensorAuxNames{};
    AuxiliaryNames nameStrings{};
    auto pdr =
        reinterpret_cast<const pldm_compact_numeric_sensor_pdr*>(sPdr.data());

    if (sPdr.size() <
        (sizeof(pldm_compact_numeric_sensor_pdr) - sizeof(uint8_t)))
    {
        return nullptr;
    }

    if (!pdr->sensor_name_length ||
        (sPdr.size() < (sizeof(pldm_compact_numeric_sensor_pdr) -
                        sizeof(uint8_t) + pdr->sensor_name_length)))
    {
        return nullptr;
    }

    std::string nameString(reinterpret_cast<const char*>(pdr->sensor_name),
                           pdr->sensor_name_length);
    nameStrings.emplace_back(
        std::make_pair("en", pldm::utils::trimNameForDbus(nameString)));
    sensorAuxNames.emplace_back(std::move(nameStrings));

    return std::make_shared<SensorAuxiliaryNames>(pdr->sensor_id, 1,
                                                  std::move(sensorAuxNames));
}

std::shared_ptr<pldm_compact_numeric_sensor_pdr>
    Terminus::parseCompactNumericSensorPDR(const std::vector<uint8_t>& sPdr)
{
    auto pdr =
        reinterpret_cast<const pldm_compact_numeric_sensor_pdr*>(sPdr.data());
    if (sPdr.size() < sizeof(pldm_compact_numeric_sensor_pdr))
    {
        // Handle error: input data too small to contain valid pdr
        return nullptr;
    }
    auto parsedPdr = std::make_shared<pldm_compact_numeric_sensor_pdr>();

    parsedPdr->hdr = pdr->hdr;
    parsedPdr->terminus_handle = pdr->terminus_handle;
    parsedPdr->sensor_id = pdr->sensor_id;
    parsedPdr->entity_type = pdr->entity_type;
    parsedPdr->entity_instance = pdr->entity_instance;
    parsedPdr->container_id = pdr->container_id;
    parsedPdr->sensor_name_length = pdr->sensor_name_length;
    parsedPdr->base_unit = pdr->base_unit;
    parsedPdr->unit_modifier = pdr->unit_modifier;
    parsedPdr->occurrence_rate = pdr->occurrence_rate;
    parsedPdr->range_field_support = pdr->range_field_support;
    parsedPdr->warning_high = pdr->warning_high;
    parsedPdr->warning_low = pdr->warning_low;
    parsedPdr->critical_high = pdr->critical_high;
    parsedPdr->critical_low = pdr->critical_low;
    parsedPdr->fatal_high = pdr->fatal_high;
    parsedPdr->fatal_low = pdr->fatal_low;
    return parsedPdr;
}

void Terminus::addCompactNumericSensor(
    const std::shared_ptr<pldm_compact_numeric_sensor_pdr> pdr)
{
    uint16_t sensorId = pdr->sensor_id;
    std::string sensorName = "PLDM_Device_" + std::to_string(sensorId) + "_" +
                             std::to_string(tid);

    auto sensorAuxiliaryNames = getSensorAuxiliaryNames(sensorId);
    if (sensorAuxiliaryNames)
    {
        const auto& [sensorId, sensorCnt, sensorNames] = *sensorAuxiliaryNames;
        if (sensorCnt == 1)
        {
            for (const auto& [languageTag, name] : sensorNames[0])
            {
                if (languageTag == "en")
                {
                    {
                        sensorName = name + "_" + std::to_string(sensorId) +
                                     "_" + std::to_string(tid);
                    }
                }
            }
        }
    }

    try
    {
        auto sensor = std::make_shared<NumericSensor>(
            tid, true, pdr, sensorName, inventoryPath);
        lg2::info("Created Compact NumericSensor {NAME}", "NAME", sensorName);
        numericSensors.emplace_back(sensor);
    }
    catch (const sdbusplus::exception_t& e)
    {
        lg2::error(
            "Failed to create Compact NumericSensor. error - {ERROR} sensorname - {NAME}",
            "ERROR", e, "NAME", sensorName);
    }
}

} // namespace platform_mc
} // namespace pldm

#include "spacecan_services.h"

const char *spacecan_service_name(uint8_t service)
{
    switch ((spacecan_service_t)service) {
    case SPACECAN_SERVICE_HOUSEKEEPING:
        return "housekeeping";
    case SPACECAN_SERVICE_PARAMETER:
        return "parameter";
    default:
        return "unknown";
    }
}

const char *spacecan_subtype_name(uint8_t service, uint8_t subtype)
{
    switch ((spacecan_service_t)service) {
    case SPACECAN_SERVICE_HOUSEKEEPING:
        switch ((spacecan_housekeeping_subtype_t)subtype) {
        case SPACECAN_HK_SUBTYPE_REPORT:
            return "housekeeping.report";
        default:
            return "housekeeping.unknown";
        }

    case SPACECAN_SERVICE_PARAMETER:
        switch ((spacecan_parameter_subtype_t)subtype) {
        case SPACECAN_PARAMETER_SUBTYPE_GET:
            return "parameter.get";
        case SPACECAN_PARAMETER_SUBTYPE_SET:
            return "parameter.set";
        case SPACECAN_PARAMETER_SUBTYPE_VALUE:
            return "parameter.value";
        default:
            return "parameter.unknown";
        }

    default:
        return "unknown";
    }
}

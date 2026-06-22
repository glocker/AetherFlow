#ifndef AETHERFLOW_SPACECAN_SERVICES_H
#define AETHERFLOW_SPACECAN_SERVICES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Service identifiers used by LibreCube/SpaceCAN style application packets. */
typedef enum {
    SPACECAN_SERVICE_HOUSEKEEPING = 3,
    SPACECAN_SERVICE_PARAMETER = 20,
} spacecan_service_t;

typedef enum {
    SPACECAN_HK_SUBTYPE_REPORT = 25,
} spacecan_housekeeping_subtype_t;

typedef enum {
    SPACECAN_PARAMETER_SUBTYPE_GET = 1,
    SPACECAN_PARAMETER_SUBTYPE_SET = 2,
    SPACECAN_PARAMETER_SUBTYPE_VALUE = 3,
} spacecan_parameter_subtype_t;

const char *spacecan_service_name(uint8_t service);
const char *spacecan_subtype_name(uint8_t service, uint8_t subtype);

#ifdef __cplusplus
}
#endif

#endif /* AETHERFLOW_SPACECAN_SERVICES_H */

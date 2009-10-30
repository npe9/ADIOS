
#ifdef _NOMPI
    /* Sequential processes can use the library compiled with -D_NOMPI */
#   define ADIOS_EMPTY_TRANSPORTS
#else
    /* Parallel applications should use MPI to communicate file info and slices of data */
#endif

#include "adios.h"
#include "adios_transport_hooks.h"
#include "adios_bp_v1.h"
#include "adios_internals.h"

void adios_init_transports (struct adios_transport_struct ** t)
{
    *t = (struct adios_transport_struct *)
             calloc (ADIOS_METHOD_COUNT, sizeof (struct adios_transport_struct));

    ADIOS_INIT_TRANSPORTS_SETUP
}

int adios_parse_method (const char * buf, enum ADIOS_IO_METHOD * method
                       ,int * requires_group_comm
                       )
{
    ADIOS_PARSE_METHOD_SETUP

    *method = ADIOS_METHOD_UNKNOWN;
    *requires_group_comm = 0;

    return 0;
}

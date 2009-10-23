#include "../config.h"
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#include "common_adios.h"
#include "adios_internals.h"
#include "futils.h"

#ifdef __cplusplus
extern "C"  /* prevent C++ name mangling */
#endif

extern int adios_errno;

///////////////////////////////////////////////////////////////////////////////
void FC_FUNC_(adiosf_init, ADIOSF_INIT) (const char * config, int * err, int config_size)
{
    char * buf1 = 0;

    buf1 = futils_fstr_to_cstr (config, config_size);
    if (buf1 != 0) {
        *err = common_adios_init (buf1);
        free (buf1);
    } else {
        *err = -adios_errno;
    }
}

///////////////////////////////////////////////////////////////////////////////
void FC_FUNC_(adiosf_init_local, ADIOSF_INIT_LOCAL) (int * err)
{
    *err = common_adios_init_local ();
}

///////////////////////////////////////////////////////////////////////////////
void FC_FUNC_(adiosf_finalize, ADIOSF_FINALIZE) (int * mype, int * err)
{
    *err = common_adios_finalize (*mype);
}

///////////////////////////////////////////////////////////////////////////////
void FC_FUNC_(adiosf_allocate_buffer, ADIOSF_ALLOCATE_BUFFER) (int * err)
{
    *err = common_adios_allocate_buffer ();
}

///////////////////////////////////////////////////////////////////////////////
void FC_FUNC_(adiosf_open, ADIOSF_OPEN) 
    (int64_t * fd, const char * group_name, const char * name
    ,const char * mode, int * err
    ,int group_name_size, int name_size, int mode_size
    )
{
    char * buf1 = 0;
    char * buf2 = 0;
    char * buf3 = 0;

    buf1 = futils_fstr_to_cstr (group_name, group_name_size);
    buf2 = futils_fstr_to_cstr (name, name_size);
    buf3 = futils_fstr_to_cstr (mode, mode_size);

    if (buf1 != 0 && buf2 != 0 && buf3 != 0) {
        *err = common_adios_open (fd, buf1, buf2, buf3);
        free (buf1);
        free (buf2);
        free (buf3);
    } else {
        *err = -adios_errno;
    }
}

///////////////////////////////////////////////////////////////////////////////
void FC_FUNC_(adiosf_group_size, ADIOSF_GROUP_SIZE) 
    (int64_t * fd_p, int64_t * data_size
    ,int64_t * total_size, void * comm, int * err
    )
{
    *err = common_adios_group_size (*fd_p, (uint64_t) *data_size
                            ,(uint64_t *) total_size, comm
                            );
}

///////////////////////////////////////////////////////////////////////////////
#include "stdio.h"
/* This Fortran api function is a bit different from the C api funcion, but
 * they call the same common_adios_write().
 * Difference: if the variable is string type then we need to convert
 * the void * var to a C string (add \0 to the end)
 * We rely on the assumption/fact that Fortran compilers pass on the 
 * length of a character array in an extra integer argument, even if
 * the C function declares a void* array in the argument list. 
 */
void FC_FUNC_(adiosf_write, ADIOSF_WRITE) 
    (int64_t * fd_p, const char * name, void * var, int * err
    ,int name_size, int var_size
    )
{
    struct adios_file_struct * fd = (struct adios_file_struct *) *fd_p;
    if (!fd)
    {
        fprintf (stderr, "Invalid handle passed to adios_write\n");
        *err = 1;
        return;
    }

    struct adios_var_struct * v = fd->group->vars;
    struct adios_method_list_struct * m = fd->group->methods;

    if (m && m->next == NULL && m->method->m == ADIOS_METHOD_NULL)
    {
        // nothing to do so just return
        *err = 0;
        return;
    }

    char * buf1 = 0;
    buf1 = futils_fstr_to_cstr (name, name_size);

    //printf("  -- adiosf_write: name=[%s] var size = %d\n", buf1, var_size);

    if (!buf1) {
        *err = -adios_errno;
        return;
    }

    v = adios_find_var_by_name (v, buf1, fd->group->all_unique_var_names);

    if (!v)
    {
        fprintf (stderr, "Bad var name (ignored): '%s'\n", buf1);
        *err = 1;
        return;
    }

    if (fd->mode == adios_mode_read)
    {
        if (   strcasecmp (buf1, fd->group->group_comm)
            && v->is_dim != adios_flag_yes
           )
        {
            fprintf (stderr, "write attempted on %s in %s.  This was opened for read\n" ,buf1 , fd->name);
            *err = 1;
            return;
        }
    }

    if (!var)
    {
        fprintf (stderr, "Invalid data: %s\n", buf1);
        *err = 1;
        return;
    }

    if (v->dimensions)
    {
        v->data = var;
    }
    else
    {
        uint64_t element_size = adios_get_type_size (v->type, var);

        switch (v->type)
        {
            case adios_byte:
            case adios_short:
            case adios_integer:
            case adios_long:
            case adios_unsigned_byte:
            case adios_unsigned_short:
            case adios_unsigned_integer:
            case adios_unsigned_long:
            case adios_real:
            case adios_double:
            case adios_long_double:
            case adios_complex:
            case adios_double_complex:
                v->data = malloc (element_size);
                if (!v->data)
                {
                    fprintf (stderr, "cannot allocate %llu bytes to copy "
                                     "scalar %s\n"
                            ,element_size
                            ,v->name
                            );

                    *err = 1;
                    return;
                }

                memcpy ((char *) v->data, var, element_size);
                break;
            case adios_string:
                v->data = futils_fstr_to_cstr (var, var_size);
                if (!v->data)
                {
                    fprintf (stderr, "cannot allocate %llu bytes to copy "
                                     "scalar %s\n"
                            ,element_size
                            ,v->name
                            );

                    *err = 1;
                    return;
                }
                break;

            default:
                v->data = 0;
                break;
        }
    }

    *err = common_adios_write (fd, v, var);
    free (buf1);
}


///////////////////////////////////////////////////////////////////////////////
void FC_FUNC_(adiosf_get_write_buffer, ADIOSF_GET_WRITE_BUFFER) 
    (int64_t * fd_p, const char * name
    ,int64_t * size
    ,void ** buffer, int * err, int name_size
    )
{
    char * buf1 = 0;

    buf1 = futils_fstr_to_cstr (name, name_size);

    if (buf1 != 0) {
        *err = common_adios_get_write_buffer (*fd_p, buf1, (uint64_t *) size, buffer);
        free (buf1);
    } else {
        *err = -adios_errno;
    }
}

///////////////////////////////////////////////////////////////////////////////
void FC_FUNC_(adiosf_read, ADIOSF_READ) 
    (int64_t * fd_p, const char * name, void * buffer
    ,int64_t * buffer_size, int * err, int name_size
    )
{
    char * buf1 = 0;

    buf1 = futils_fstr_to_cstr (name, name_size);

    if (buf1 != 0) {
        *err = common_adios_read (*fd_p, buf1, buffer, *buffer_size);
        free (buf1);
    } else {
        *err = -adios_errno;
    }
}

///////////////////////////////////////////////////////////////////////////////
void FC_FUNC_(adiosf_set_path, ADIOSF_SET_PATH) 
    (int64_t * fd_p, const char * path, int * err, int path_size)
{
    char * buf1 = 0;

    buf1 = futils_fstr_to_cstr (path, path_size);

    if (buf1 != 0) {
        *err = common_adios_set_path (*fd_p, buf1);
        free (buf1);
    } else {
        *err = -adios_errno;
    }

    free (buf1);
}

///////////////////////////////////////////////////////////////////////////////
void FC_FUNC_(adiosf_set_path_var, ADIOSF_SET_PATH_VAR) 
    (int64_t * fd_p, const char * path, const char * name, int * err, int path_size, int name_size)
{
    char * buf1 = 0;
    char * buf2 = 0;

    buf1 = futils_fstr_to_cstr (path, path_size);
    buf2 = futils_fstr_to_cstr (name, name_size);

    if (buf1 != 0 && buf2 != 0) {
        *err = common_adios_set_path_var (*fd_p, buf1, buf2);
        free (buf1);
        free (buf2);
    } else {
        *err = -adios_errno;
    }
}

///////////////////////////////////////////////////////////////////////////////
// hint that we reached the end of an iteration (for asynchronous pacing)
void FC_FUNC_(adiosf_end_iteration, ADIOSF_END_ITERATION) (int * err)
{
    *err = common_adios_end_iteration ();
}

///////////////////////////////////////////////////////////////////////////////
// hint to start communicating
void FC_FUNC_(adiosf_start_calculation, ADIOSF_START_CALCULATION) (int * err)
{
    *err = common_adios_start_calculation ();
}

///////////////////////////////////////////////////////////////////////////////
// hint to stop communicating
void FC_FUNC_(adiosf_stop_calculation, ADIOSF_STOP_CALCULATION) (int * err)
{
    *err = common_adios_stop_calculation ();
}

///////////////////////////////////////////////////////////////////////////////
void FC_FUNC_(adiosf_close, ADIOSF_CLOSE) (int64_t * fd_p, int * err)
{
    *err = common_adios_close (*fd_p);
}

//////////////////////////////////////////////////////////////////////////////
// Methods normally only called by the XML parser
//////////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////////
// adios_common_declare_group is in adios_internals.c
// group a list of vars into a composite group
void FC_FUNC_(adiosf_declare_group, ADIOSF_DECLARE_GROUP) 
    (int64_t * id, const char * name
    ,const char * coordination_comm
    ,const char * coordination_var
    ,const char * time_index, int * err
    ,int name_size, int coordination_comm_size
    ,int coordination_var_size, int time_index_size
    )
{
    char * buf1 = 0;
    char * buf2 = 0;
    char * buf3 = 0;
    char * buf4 = 0;

    buf1 = futils_fstr_to_cstr (name, name_size);
    buf2 = futils_fstr_to_cstr (coordination_comm, coordination_comm_size);
    buf3 = futils_fstr_to_cstr (coordination_var, coordination_var_size);
    buf4 = futils_fstr_to_cstr (time_index, time_index_size);

    if (buf1 != 0 && buf2 != 0 && buf3 != 0 && buf4 != 0) {
        *err = adios_common_declare_group (id, buf1, adios_flag_yes, buf2, buf3, buf4);
        free (buf1);
        free (buf2);
        free (buf3);
        free (buf4);
    } else {
        *err = -adios_errno;
    }
}

///////////////////////////////////////////////////////////////////////////////
// adios_common_define_var is in adios_internals.c
// declare a single var as an entry in a group
void FC_FUNC_(adiosf_define_var, ADIOSF_DEFINE_VAR) 
    (int64_t * group_id, const char * name
    ,const char * path, int * type
    ,const char * dimensions
    ,const char * global_dimensions
    ,const char * local_offsets, int * err
    ,int name_size, int path_size, int dimensions_size
    ,int global_dimensions_size, int local_offsets_size
    )
{
    char * buf1 = 0;
    char * buf2 = 0;
    char * buf3 = 0;
    char * buf4 = 0;
    char * buf5 = 0;

    buf1 = futils_fstr_to_cstr (name, name_size);
    buf2 = futils_fstr_to_cstr (path, path_size);
    buf3 = futils_fstr_to_cstr (dimensions, dimensions_size);
    buf4 = futils_fstr_to_cstr (global_dimensions, global_dimensions_size);
    buf5 = futils_fstr_to_cstr (local_offsets, local_offsets_size);

    if (buf1 != 0 && buf2 != 0 && buf3 != 0 && buf4 != 0 && buf5 != 0) {
        *err = adios_common_define_var (*group_id, buf1, buf2
                                       ,(enum ADIOS_DATATYPES) *type
                                       ,buf3, buf4, buf5
                                       );

        free (buf1);
        free (buf2);
        free (buf3);
        free (buf4);
        free (buf5);
    } else {
        *err = -adios_errno;
    }
}

///////////////////////////////////////////////////////////////////////////////
// adios_common_define_attribute is in adios_internals.c
void FC_FUNC_(adiosf_define_attribute, ADIOSF_DEFINE_ATTRIBUTE) 
    (int64_t * group, const char * name
    ,const char * path, int type, const char * value
    ,const char * var, int * err
    ,int name_size, int path_size, int value_size
    ,int var_size
    )
{
    char * buf1 = 0;
    char * buf2 = 0;
    char * buf3 = 0;
    char * buf4 = 0;

    buf1 = futils_fstr_to_cstr (name, name_size);
    buf2 = futils_fstr_to_cstr (path, path_size);
    buf3 = futils_fstr_to_cstr (value, value_size);
    buf4 = futils_fstr_to_cstr (var, var_size);

    if (buf1 != 0 && buf2 != 0 && buf3 != 0 && buf4 != 0) {
        *err = adios_common_define_attribute (*group, buf1, buf2
                                             ,(enum ADIOS_DATATYPES) type, buf3
                                             ,buf4
                                             );

        free (buf1);
        free (buf2);
        free (buf3);
        free (buf4);
    } else {
        *err = -adios_errno;
    }
}

///////////////////////////////////////////////////////////////////////////////
// adios_common_select_method is in adios_internals.c
void FC_FUNC_(adiosf_select_method, ADIOSF_SELECT_METHOD) 
    (int * priority, const char * method
    ,const char * parameters, const char * group
    ,const char * base_path, int * iters, int * err
    ,int method_size, int parameters_size
    ,int group_size, int base_path_size
    )
{
    char * buf1 = 0;
    char * buf2 = 0;
    char * buf3 = 0;
    char * buf4 = 0;

    buf1 = futils_fstr_to_cstr (method, method_size);
    buf2 = futils_fstr_to_cstr (parameters, parameters_size);
    buf3 = futils_fstr_to_cstr (group, group_size);
    buf4 = futils_fstr_to_cstr (base_path, base_path_size);

    if (buf1 != 0 && buf2 != 0 && buf3 != 0 && buf4 != 0) {
        *err = adios_common_select_method (*priority, buf1, buf2, buf3, buf4,*iters);

        free (buf1);
        free (buf2);
        free (buf3);
        free (buf4);
    } else {
        *err = -adios_errno;
    }
}

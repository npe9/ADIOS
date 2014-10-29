/*
  read_icee.c       
  Goal: to create evpath io connection layer in conjunction with 
  write/adios_icee.c
*/
// system libraries
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/times.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>

// evpath libraries
#include <ffs.h>
#include <atl.h>
//#include <gen_thread.h>
#include <evpath.h>

#ifdef __MACH__
#include <mach/clock.h>
#include <mach/mach.h>
#endif

// local libraries
#include "config.h"
#include "public/adios.h"
#include "public/adios_types.h"
#include "public/adios_read_v2.h"
#include "core/adios_read_hooks.h"
#include "core/adios_logger.h"
#include "public/adios_error.h"
//#include "core/adios_icee.h"

// conditional libraries
#ifdef DMALLOC
#include "dmalloc.h"
#endif

#define MALLOC0(type_t, var) var = malloc(sizeof(type_t)); memset(var, '\0', sizeof(type_t));
#define MYALLOC(var, nbyte)       \
    if (nbyte) {                  \
        var = malloc(nbyte);      \
        assert((var != NULL) && "malloc failure");      \
        memset(var, '\0', nbyte); \
    } else                        \
        var = NULL;

#define MYFREE(var) free(var); var = NULL;

#define MYMIN(a,b)               \
    ({ __typeof__ (a) _a = (a);  \
        __typeof__ (b) _b = (b); \
        _a < _b ? _a : _b; })

#define MYMAX(a,b)               \
    ({ __typeof__ (a) _a = (a);  \
        __typeof__ (b) _b = (b); \
        _a > _b ? _a : _b; })

///////////////////////////
// Global Variables
///////////////////////////
#include "core/adios_icee.h"

#define DUMP(fmt, ...) fprintf(stderr, ">>> "fmt"\n", ## __VA_ARGS__); 


/* Data Structure
+ fileinfo 1.1 - fileinfo 1.2 - ...
|    + var 1.1      + var 1.2 
|    + var 2.1      + var 2.2
|    + ...          + ...
+ fileinfo 2.1 - fileinfo 2.2 - ...

x.y (x=filename, y=version)
 */

typedef struct icee_llist
{
    void*             item;
    struct icee_llist* next;
} icee_llist_t, *icee_llist_ptr_t;

int (*icee_llist_fp)(const void*, const void*);

icee_llist_ptr_t
icee_llist_create(void* item)
{
    icee_llist_ptr_t node = malloc(sizeof(icee_llist_t));
    node->item = item;
    node->next = NULL;
    
    return node;
}

icee_llist_ptr_t
icee_llist_append(const icee_llist_ptr_t root, void* item)
{
    assert(root != NULL);
        
    icee_llist_ptr_t p = root;
    
    while (p->next != NULL)
        p = p->next;

    icee_llist_ptr_t node = malloc(sizeof(icee_llist_t));
    node->item = item;
    node->next = NULL;
    
    p->next = node;

    return node;
}

icee_llist_ptr_t
icee_llist_search(const icee_llist_ptr_t root, int (*fp)(const void*, const void*), const void* arg)
{
    icee_llist_ptr_t p = root;
    
    while (p != NULL)
    {
        if ((*fp)(p->item, arg)) break;
        p = p->next;
    }

    return p;
}

void
icee_llist_map(const icee_llist_ptr_t root, void (*fp)(const void*))
{
    icee_llist_ptr_t p = root;
    
    while (p != NULL)
    {
        (*fp)(p->item);
        p = p->next;
    }
}

int icee_fileinfo_checkname(const void* item, const void* fname)
{
    icee_fileinfo_rec_ptr_t fp = (icee_fileinfo_rec_ptr_t) item;
    if (strcmp(fp->fname, (char *)fname) == 0)
        return 1;
    else
        return 0;
}

void icee_varinfo_print(const icee_varinfo_rec_ptr_t vp);
void icee_fileinfo_print(const void* item)
{
    icee_fileinfo_rec_ptr_t fp = (icee_fileinfo_rec_ptr_t) item;

    fprintf(stderr, "===== fileinfo (%p) =====\n", fp);

    if (fp)
    {
        fprintf(stderr, "%10s : %s\n", "fname", fp->fname);
        fprintf(stderr, "%10s : %d\n", "nvars", fp->nvars);
        fprintf(stderr, "%10s : %d\n", "nchunks", fp->nchunks);
        fprintf(stderr, "%10s : %d\n", "comm_size", fp->comm_size);
        fprintf(stderr, "%10s : %d\n", "comm_rank", fp->comm_rank);
        fprintf(stderr, "%10s : %d\n", "merge_count", fp->merge_count);
        fprintf(stderr, "%10s : %d\n", "timestep", fp->timestep);
        fprintf(stderr, "%10s : %p\n", "varinfo", fp->varinfo);
        fprintf(stderr, "%10s : %p\n", "next", fp->next);

        icee_varinfo_rec_ptr_t vp = fp->varinfo;

        while (vp != NULL)
        {
            icee_varinfo_print(vp);
            vp = vp->next; 
        }

        if (fp->next != NULL)
            icee_fileinfo_print(fp->next);
    }
    else
    {
        fprintf(stderr, "fileinfo is invalid\n");
    }
}

icee_llist_ptr_t icee_filelist = NULL;

void icee_fileinfo_append(const icee_fileinfo_rec_ptr_t root, icee_fileinfo_rec_ptr_t fp)
{
    assert(root != NULL);
    
    icee_fileinfo_rec_ptr_t p = root;

    while ((p->timestep != fp->timestep) && (p->next != NULL))
    {
        p = p->next; 
    }

    if (p->timestep == fp->timestep)
    {
        icee_varinfo_rec_ptr_t v = p->varinfo;
        
        while (v->next != NULL)
            v = v->next;

        v->next = fp->varinfo;
        p->merge_count++;

        free(fp);
    }
    else
    {
        p->next = fp;
    }

    /*
    while (p->next != NULL)
    {
        p = p->next;
    }

    p->next = fp;
    */
}

void icee_fileinfo_copy(icee_fileinfo_rec_ptr_t dest, const icee_fileinfo_rec_ptr_t src)
{
    memcpy(dest, src, sizeof(icee_fileinfo_rec_t));

    dest->fname = strdup(src->fname);
    dest->next = NULL;
}

void icee_fileinfo_free(icee_fileinfo_rec_ptr_t fp)
{
    icee_varinfo_rec_ptr_t vp = fp->varinfo;

    while (vp != NULL)
    {
        free(vp->varname);
        free(vp->gdims);
        free(vp->ldims);
        free(vp->offsets);
        free(vp->data);

        icee_varinfo_rec_ptr_t prev = vp;
        vp = vp->next;

        free(prev);
    }

    free(fp);
    fp = NULL;    
}

icee_varinfo_rec_ptr_t
icee_varinfo_search_byname(icee_varinfo_rec_ptr_t head, const char* name)
{
    icee_varinfo_rec_ptr_t vp = head;

    while (vp != NULL)
    {
        if (strcmp(vp->varname, name) == 0)
            break;
        vp = vp->next;
    }

    return vp;
}

void icee_varinfo_copy(icee_varinfo_rec_ptr_t dest, const icee_varinfo_rec_ptr_t src)
{
    memcpy(dest, src, sizeof(icee_varinfo_rec_t));

    dest->varname = strdup(src->varname);

    uint64_t dimsize = src->ndims * sizeof(uint64_t);
    dest->gdims = malloc(dimsize);
    dest->ldims = malloc(dimsize);
    dest->offsets = malloc(dimsize);

    memcpy(dest->gdims, src->gdims, dimsize);
    memcpy(dest->ldims, src->ldims, dimsize);
    memcpy(dest->offsets, src->offsets, dimsize);
    
    dest->data = malloc(src->varlen);
    memcpy(dest->data, src->data, src->varlen);

    dest->next = NULL;
}

double dsum(const int len, const double* data)
{
    double s = 0.0;
    int i;
    for (i=0; i<len; i++)
        s += data[i];

    return s;
}

void icee_data_print(const int type, const uint64_t varlen, const char* data)
{
    fprintf(stderr, "%10s : %p\n", "*data", data);
    switch (type)
    {
    case 2: // int
        if (data)
        {
            fprintf(stderr, "%10s : %d,%d,%d,...\n", "data", 
                    ((int*)data)[0], ((int*)data)[1], ((int*)data)[2]);
            fprintf(stderr, "%10s : %g\n", "sum", dsum(varlen/8, (double*)data));
        }
        break;
    case 6: // double
        if (data)
        {
            fprintf(stderr, "%10s : %g,%g,%g,...\n", "data", 
                    ((double*)data)[0], ((double*)data)[1], ((double*)data)[2]);
            fprintf(stderr, "%10s : %g\n", "sum", dsum(varlen/8, (double*)data));
        }
        break;
    }
}

void icee_dims_print(const char* name, const int ndims, const uint64_t *dims)
{
    
    switch (ndims)
    {
    case 0:
        fprintf(stderr, "%10s : none\n", name);
        break;
    case 1:
        fprintf(stderr, "%10s : %llu\n", name, dims[0]);
        break;
    case 2:
        fprintf(stderr, "%10s : %llu,%llu\n", 
                name, dims[0], dims[1]);
        break;
    case 3:
        fprintf(stderr, "%10s : %llu,%llu,%llu\n", 
                name, dims[0], dims[1], dims[2]);
        break;
    default:
        fprintf(stderr, "%10s : %llu,%llu,%llu,...\n", 
                name, dims[0], dims[1], dims[2]);
        break;
    }
}

void icee_varinfo_print(const icee_varinfo_rec_ptr_t vp)
{
    fprintf(stderr, "===== varinfo (%p) =====\n", vp);

    if (vp)
    {
        fprintf(stderr, "%10s : %s\n", "varname", vp->varname);
        fprintf(stderr, "%10s : %d\n", "varid", vp->varid);
        fprintf(stderr, "%10s : %d\n", "type", vp->type);
        fprintf(stderr, "%10s : %d\n", "typesize", vp->typesize);
        fprintf(stderr, "%10s : %d\n", "ndims", vp->ndims);
        icee_dims_print("gdims", vp->ndims, vp->gdims);
        icee_dims_print("ldims", vp->ndims, vp->ldims);
        icee_dims_print("offsets", vp->ndims, vp->offsets);
        fprintf(stderr, "%10s : %llu\n", "varlen", vp->varlen);
        icee_data_print(vp->type, vp->varlen, vp->data);
    }
    else
    {
        fprintf(stderr, "varinfo is invalid\n");
    }
}

void icee_sel_bb_print(const ADIOS_SELECTION *sel)
{
    fprintf(stderr, "===== selection (%p) =====\n", sel);

    if (sel)
    {
        switch(sel->type)
        {
        case ADIOS_SELECTION_WRITEBLOCK:
            fprintf(stderr, "%10s : %s\n", "type", "writeblock");
            break;
        case ADIOS_SELECTION_BOUNDINGBOX:
            fprintf(stderr, "%10s : %s\n", "type", "boundingbox");
            fprintf(stderr, "%10s : %d\n", "ndims", sel->u.bb.ndim);
            icee_dims_print("start", sel->u.bb.ndim, sel->u.bb.start);
            icee_dims_print("count", sel->u.bb.ndim, sel->u.bb.count);
            break;
        case ADIOS_SELECTION_AUTO:
            fprintf(stderr, "%10s : %s\n", "type", "auto");
            break;
        case ADIOS_SELECTION_POINTS:
            fprintf(stderr, "%10s : %s\n", "type", "points");
            break;
        default:
            fprintf(stderr, "%10s : %s\n", "type", "undefined");
            break;
        }
    }
    else
    {
        fprintf(stderr, "selection is invalid\n");
    }
}

static int
icee_fileinfo_handler(CManager cm, void *vevent, void *client_data, attr_list attrs)
{
    icee_fileinfo_rec_ptr_t event = vevent;
    log_debug("%s (%s)\n", __FUNCTION__, event->fname);

    icee_fileinfo_rec_ptr_t lfp = malloc(sizeof(icee_fileinfo_rec_t));
    icee_fileinfo_copy(lfp, event);
    lfp->merge_count = 1;

    icee_varinfo_rec_ptr_t eventvp = event->varinfo;
    icee_varinfo_rec_ptr_t *lvpp = &lfp->varinfo;

    while (eventvp != NULL)
    {
        *lvpp = malloc(sizeof(icee_varinfo_rec_t));
        icee_varinfo_copy(*lvpp, eventvp);
        if (adios_verbose_level > 3) DUMP("id,name = %d,%s", (*lvpp)->varid, (*lvpp)->varname);
        
        lvpp = &(*lvpp)->next;
        eventvp = eventvp->next;
    }

    if (icee_filelist == NULL)
        icee_filelist = icee_llist_create((void *)lfp);
    else
    {
        icee_llist_ptr_t head;
        head = icee_llist_search(icee_filelist, icee_fileinfo_checkname, (const void*) lfp->fname);

        if (head == NULL)
            icee_llist_append(icee_filelist, (void*) lfp);
        else
            icee_fileinfo_append((icee_fileinfo_rec_ptr_t)head->item, lfp);
    }

    if (adios_verbose_level > 5)
        icee_llist_map(icee_filelist, icee_fileinfo_print);

    return 1;
}

static int adios_read_icee_initialized = 0;

CManager icee_read_cm;

/* Row-ordered matrix representatin */
typedef struct {
    int      typesize;
    int      ndims;
    uint64_t dims[10];
    uint64_t accumdims[10];
    void*    data;
} icee_matrix_t;

/* View (subset) representation */
typedef struct {
    uint64_t vdims[10];
    uint64_t offsets[10];
    int      leastcontiguousdim;
    icee_matrix_t* mat;
} icee_matrix_view_t;

void 
init_mat (icee_matrix_t *m, 
          int typesize,
          int ndims,
          const uint64_t *dims,
          void *data)
{
    assert(ndims <= 10);
    m->typesize = typesize;
    m->ndims = ndims;
    memcpy(m->dims, dims, ndims * sizeof(uint64_t));

    int i;
    uint64_t p = 1;
    for (i=ndims-1; i>=0; i--)
    {
        m->accumdims[i] = p;
        p *= dims[i];
    }

    m->data = data;
}

void 
init_view (icee_matrix_view_t *v,
           icee_matrix_t *m,
           const uint64_t *vdims,
           const uint64_t *offsets)
{
    v->mat = m;
    memcpy(v->vdims, vdims, m->ndims * sizeof(uint64_t));
    memcpy(v->offsets, offsets, m->ndims * sizeof(uint64_t));

    int i;
    for (i=m->ndims-1; i>=0; i--)
    {
        v->leastcontiguousdim = i;
        if (vdims[i] + offsets[i] == m->dims[i])
            break;
    }
}

/* Copy data between two views. Dimension and size should match */
void
copy_view (icee_matrix_view_t *dest, icee_matrix_view_t *src)
{
    assert(dest->mat->ndims == src->mat->ndims);
    
    int i;
    for (i=0; i<dest->mat->ndims; i++)
        assert(dest->vdims[i] == src->vdims[i]);

    // Contiguous merging
    if ((dest->leastcontiguousdim == 1) && (src->leastcontiguousdim==1))
    {
        int s, d;
        d = dest->offsets[0];
        s = src->offsets[0];
        memcpy(dest->mat->data + d * dest->mat->typesize, 
               src->mat->data + s * dest->mat->typesize, 
               dest->vdims[0] * dest->mat->accumdims[0] * dest->mat->typesize);

        return;
    }
    
    // Non-contiguous merging
    switch (dest->mat->ndims)
    {
    case 1:
    {
        int s, d;
        d = dest->offsets[0];
        s = src->offsets[0];
        memcpy(dest->mat->data + d * dest->mat->typesize, 
               src->mat->data + s * dest->mat->typesize, 
               dest->vdims[0] * dest->mat->typesize);
        break;
    }
    case 2:
    {
        int i, s, d;
        for (i=0; i<dest->vdims[0]; i++)
        {
            d = (i + dest->offsets[0]) * dest->mat->accumdims[0] 
                + dest->offsets[1];
            s = (i + src->offsets[0]) * src->mat->accumdims[0] 
                + src->offsets[1];
            memcpy(dest->mat->data + d * dest->mat->typesize, 
                   src->mat->data + s * dest->mat->typesize, 
                   dest->vdims[1] * dest->mat->typesize);
        }
        break;
    }
    case 3:
    {
        int i, j, s, d;
        for (i=0; i<dest->vdims[0]; i++)
        {
            for (j=0; j<dest->vdims[1]; j++)
            {
                d = (i + dest->offsets[0]) * dest->mat->accumdims[0] 
                    + (j + dest->offsets[1]) * dest->mat->accumdims[1]
                    + dest->offsets[2];
                s = (i + src->offsets[0]) * src->mat->accumdims[0] 
                    + (j + src->offsets[1]) * src->mat->accumdims[1] 
                    + src->offsets[2];
                memcpy(dest->mat->data + d * dest->mat->typesize, 
                       src->mat->data + s * dest->mat->typesize, 
                       dest->vdims[2] * dest->mat->typesize);
            }
        }
        break;
    }
    default:
        adios_error(err_expected_read_size_mismatch,
                    "The variable dimension is out of the range. ",
                    "Not yet supported by ICEE\n");
        break;
    }
}

/********** Core ADIOS Read functions. **********/

/*
 * Gathers basic MPI information; sets up reader CM.
 */
int
adios_read_icee_init_method (MPI_Comm comm, PairStruct* params)
{   
    log_debug ("%s\n", __FUNCTION__);

    int cm_port = 59997;
    char *cm_host = "localhost";
    int cm_remote_port = 59999;
    char *cm_remote_host = "localhost";
    char *cm_attr = NULL;
    attr_list contact_list;

    icee_clientinfo_rec_t *remote_server;
    int num_remote_server = 0;
    int i;

    int use_single_remote_server = 1;

    PairStruct * p = params;

    while (p)
    {
        if (!strcasecmp (p->name, "cm_attr"))
        {
            cm_attr = p->value;
        }
        else if (!strcasecmp (p->name, "cm_host"))
        {
            cm_host = p->value;
        }
        else if (!strcasecmp (p->name, "cm_port"))
        {
            cm_port = atoi(p->value);
        }
        else if (!strcasecmp (p->name, "cm_remote_host"))
        {
            cm_remote_host = p->value;
        }
        else if (!strcasecmp (p->name, "cm_remote_port"))
        {
            cm_remote_port = atoi(p->value);
        }
        else if (!strcasecmp (p->name, "remote_list"))
        {
            use_single_remote_server = 0;

            char **plist;
            int plen = 8;

            plist = malloc(plen * sizeof(char *));

            char* token = strtok(p->value, ",");
            int len = 0;
            while (token) 
            {
                plist[len] = token;

                token = strtok(NULL, ",");
                len++;
                
                if (len > plen)
                {
                    plen = plen*2;
                    realloc (plist, plen * sizeof(char *));
                }
            }

            num_remote_server = len;
            remote_server = calloc(len, sizeof(icee_clientinfo_rec_t));
            
            for (i=0; i<len; i++)
            {
                char *myparam = plist[i];
                token = strtok(myparam, ":");
                
                if (myparam[0] == ':')
                {
                    remote_server[i].client_host = "localhost";
                    remote_server[i].client_port = atoi(token);
                }
                else
                {
                    remote_server[i].client_host = token;
                    token = strtok(NULL, ":");
                    remote_server[i].client_port = atoi(token);
                }
            }

            free(plist);
        }

        p = p->next;
    }

    if (cm_attr)
    {
        contact_list = attr_list_from_string(cm_attr);
    }
    else
    {
        contact_list = create_attr_list();
        add_int_attr(contact_list, attr_atom_from_string("IP_PORT"), cm_port);
        add_string_attr(contact_list, attr_atom_from_string("IP_HOST"), cm_host);
    }

    if (use_single_remote_server)
    {
        num_remote_server = 1;
        remote_server = calloc(1, sizeof(icee_clientinfo_rec_t));
        remote_server[0].client_host = cm_remote_host;
        remote_server[0].client_port = cm_remote_port;
    }

    //log_info ("cm_attr : %s\n", cm_attr);
    log_info ("cm_host : %s\n", cm_host);
    log_info ("cm_port : %d\n", cm_port);
    for (i = 0; i < num_remote_server; i++) 
    {
        log_info ("remote_list : %s:%d\n", remote_server[i].client_host, remote_server[i].client_port);
    }

    if (!adios_read_icee_initialized)
    {
        EVstone stone, remote_stone;
        EVsource source;
        attr_list contact_list;

        icee_read_cm = CManager_create();

        // Listen first
        {
            //cm = CManager_create();
            attr_list contact_list_r;
            contact_list_r = create_attr_list();
            add_int_attr(contact_list_r, attr_atom_from_string("IP_PORT"), cm_port);
            if (CMlisten_specific(icee_read_cm, contact_list_r) == 0) 
            {
                fprintf(stderr, "error: unable to initialize connection manager.\n");
                exit(-1);
            }

            log_debug("Contact list \"%s\"\n", attr_list_to_string(contact_list_r));

            stone = EValloc_stone(icee_read_cm);
            log_debug("Stone ID: %d\n", stone);
            EVassoc_terminal_action(icee_read_cm, stone, icee_fileinfo_format_list, icee_fileinfo_handler, NULL);

            if (!CMfork_comm_thread(icee_read_cm)) 
            {
                printf("Fork of communication thread failed, exiting\n");
                exit(-1);
            }
        }

        EVstone split_stone;
        EVaction split_action;
        split_stone = EValloc_stone(icee_read_cm);
        split_action = EVassoc_split_action(icee_read_cm, split_stone, NULL);
        for (i = 0; i < num_remote_server; i++) 
        {
            //attr_list contact_list;
            EVstone remote_stone, output_stone;
            remote_stone = 0;
            output_stone = EValloc_stone(icee_read_cm);

            contact_list = create_attr_list();
            add_int_attr(contact_list, attr_atom_from_string("IP_PORT"), remote_server[i].client_port);
            add_string_attr(contact_list, attr_atom_from_string("IP_HOST"), remote_server[i].client_host);
            EVassoc_bridge_action(icee_read_cm, output_stone, contact_list, remote_stone);
            EVaction_add_split_target(icee_read_cm, split_stone, split_action, output_stone);
        }
        source = EVcreate_submit_handle(icee_read_cm, split_stone, icee_clientinfo_format_list);
        icee_clientinfo_rec_t info;
        info.client_host = cm_host;
        info.client_port = cm_port;
        info.stone_id = stone;
        
        EVsubmit(source, &info, NULL);

        adios_read_icee_initialized = 1;
    }

    free(remote_server);
    return 0;
}

ADIOS_FILE*
adios_read_icee_open_file(const char * fname, MPI_Comm comm)
{
    adios_error (err_operation_not_supported,
                 "ICEE staging method does not support file mode for reading. "
                 "Use adios_read_open() to open a staged dataset.\n");
    return NULL;
}

ADIOS_FILE*
adios_read_icee_open(const char * fname,
                     MPI_Comm comm,
                     enum ADIOS_LOCKMODE lock_mode,
                     float timeout_sec)
{
    log_debug("%s\n", __FUNCTION__);

    icee_llist_ptr_t head = NULL;

    float waited_sec = 0.0;    
    while (waited_sec <= timeout_sec)
    {
        head = icee_llist_search(icee_filelist, icee_fileinfo_checkname, 
                                 (const void*) fname);
        if (head != NULL)
            break;

        usleep(0.1*1E6);
        waited_sec += 0.1;
    }
    
    if (head == NULL)
    {
        log_error ("No data yet\n");
        return NULL;
    }
    
    icee_fileinfo_rec_ptr_t fp = (icee_fileinfo_rec_ptr_t)head->item;
    
    while (fp->merge_count != fp->nchunks)
    {
        log_debug("Waiting the rest of blocks (%d/%d)\n", fp->merge_count, fp->nchunks);
        
        usleep(0.1*1E6);
    }

    ADIOS_FILE *adiosfile = malloc(sizeof(ADIOS_FILE));

    adiosfile->fh = (uint64_t)fp;

    adiosfile->nvars = fp->nvars;
	adiosfile->var_namelist = malloc(fp->nvars * sizeof(char *));

    icee_varinfo_rec_ptr_t vp = fp->varinfo;

    int i;
    for (i = 0; i < fp->nvars; i++)
    {
        adiosfile->var_namelist[i] = strdup(vp->varname);
        vp = vp->next;
    }

    adiosfile->nattrs = 0;
    adiosfile->attr_namelist = NULL;

    adiosfile->current_step = 0;
    adiosfile->last_step = 1;

    adiosfile->path = strdup(fname);

    adiosfile->version = -1;
    adiosfile->file_size = 0;
    adios_errno = err_no_error;        

    return adiosfile;
}

int 
adios_read_icee_finalize_method ()
{
    log_debug("%s\n", __FUNCTION__);

    if (adios_read_icee_initialized)
    {
        CManager_close(icee_read_cm);
        adios_read_icee_initialized = 0;
    }

    return 0;
}

void 
adios_read_icee_release_step(ADIOS_FILE *adiosfile) 
{
    log_debug("%s\n", __FUNCTION__);
}

int 
adios_read_icee_advance_step(ADIOS_FILE *adiosfile, int last, float timeout_sec) 
{
    log_debug("%s\n", __FUNCTION__);
    adios_errno = 0;

    icee_fileinfo_rec_ptr_t fp = (icee_fileinfo_rec_ptr_t) adiosfile->fh;
    icee_fileinfo_rec_ptr_t next = NULL;

    float waited_sec = 0.0;
    while (waited_sec <= timeout_sec)
    {
        next = fp->next;

        if (next != NULL)
            break;

        usleep(0.1*1E6);
        waited_sec += 0.1;
    }
    
    if (next != NULL)
    {
        icee_llist_ptr_t head = NULL;
        head = icee_llist_search(icee_filelist, icee_fileinfo_checkname, 
                                 (const void*) fp->fname);
        assert(head != NULL);

        head->item = next;
        adiosfile->fh = (uint64_t) next;

        icee_fileinfo_free(fp);
    }
    else
        adios_error (err_step_notready, 
                     "No more data yet\n");

    return adios_errno;
}

int 
adios_read_icee_close(ADIOS_FILE * fp)
{
    log_debug("%s\n", __FUNCTION__);

    return 0;
}

ADIOS_FILE *
adios_read_icee_fopen(const char *fname, MPI_Comm comm) 
{
    log_error("No support yet: %s\n", __FUNCTION__);
    return NULL;
}

int 
adios_read_icee_is_var_timed(const ADIOS_FILE* fp, int varid) 
{  
    log_error("No support yet: %s\n", __FUNCTION__);
    return 0; 
}

void 
adios_read_icee_get_groupinfo(const ADIOS_FILE *fp, int *ngroups, char ***group_namelist, uint32_t **nvars_per_group, uint32_t **nattrs_per_group) 
{
    log_debug("%s\n", __FUNCTION__);

    icee_fileinfo_rec_ptr_t p = (icee_fileinfo_rec_ptr_t) fp->fh;

    if (p)
    {
        *ngroups = 1;
        *group_namelist = (char **) malloc (sizeof (char*));
        *group_namelist[0] = strdup ("noname");
    }
    
    return;
}

int 
adios_read_icee_check_reads(const ADIOS_FILE* fp, ADIOS_VARCHUNK** chunk) 
{ 
    log_error("No support yet: %s\n", __FUNCTION__);
    return 0; 
}

int adios_read_icee_perform_reads(const ADIOS_FILE *adiosfile, int blocking)
{
    log_debug("%s\n", __FUNCTION__);
    return 0;
}

int
adios_read_icee_inq_var_blockinfo(const ADIOS_FILE* fp,
				      ADIOS_VARINFO* varinfo)
{
    log_error("No support yet: %s\n", __FUNCTION__);
    return 0; 
}

int
adios_read_icee_inq_var_stat(const ADIOS_FILE* fp,
				 ADIOS_VARINFO* varinfo,
				 int per_step_stat,
				 int per_block_stat)
{
    log_error("No support yet: %s\n", __FUNCTION__);
    return 0; 
}


int 
adios_read_icee_schedule_read_byid(const ADIOS_FILE *adiosfile,
				       const ADIOS_SELECTION *sel,
				       int varid,
				       int from_steps,
				       int nsteps,
				       void *data)
{   
    int i;
    icee_fileinfo_rec_ptr_t fp = (icee_fileinfo_rec_ptr_t) adiosfile->fh;
    log_debug("%s (%d:%s)\n", __FUNCTION__, varid, fp->fname);
    assert(varid < fp->nvars);

    if (nsteps != 1)
    {
        adios_error (err_invalid_timestep,
                     "Only one step can be read from a stream at a time. "
                     "You requested % steps in adios_schedule_read()\n", 
                     nsteps);
        return err_invalid_timestep;
    }
    
    icee_varinfo_rec_ptr_t vp = NULL;
    vp = icee_varinfo_search_byname(fp->varinfo, adiosfile->var_namelist[varid]);
    if (adios_verbose_level > 3) icee_varinfo_print(vp);

    if (!vp)
    {
        adios_error(err_invalid_varid,
                    "Invalid variable id: %d\n",
                    varid);
        return adios_errno;
    }

    while (fp->merge_count != fp->nchunks)
    {
        log_debug("Waiting the rest of blocks (%d/%d)\n", fp->merge_count, fp->nchunks);
        
        usleep(0.1*1E6);
    }

    if (sel==0)
        memcpy(data, vp->data, vp->varlen);
    else
        switch(sel->type)
        {
        case ADIOS_SELECTION_WRITEBLOCK:
        {
            //DUMP("fp->rank: %d", fp->rank);
            //DUMP("u.block.index: %d", sel->u.block.index);

            if (fp->comm_rank != sel->u.block.index)
                adios_error(err_unspecified,
                            "Block id missmatch. "
                            "Not yet supported by ICEE\n");

            // Possible memory overwrite
            memcpy(data, vp->data, vp->varlen);
            break;
        }
        case ADIOS_SELECTION_BOUNDINGBOX:
        {
            if (vp->ndims != sel->u.bb.ndim)
                adios_error(err_invalid_dimension,
                            "Dimension mismatch\n");

            log_debug("Merging operation (total nvars: %d).\n", fp->nchunks);
            if (adios_verbose_level > 3) icee_sel_bb_print(sel);

            while (vp != NULL)
            {
                icee_matrix_t m_sel = {};
                icee_matrix_t m_var = {};
                icee_matrix_view_t v_sel = {};
                icee_matrix_view_t v_var = {};
                uint64_t start[10] = {}, count[10] = {};
                uint64_t s_offsets[10] = {}, v_offsets[10] = {};
                int i;

                if (adios_verbose_level > 3) icee_varinfo_print(vp);
                    
                init_mat(&m_sel, vp->typesize, vp->ndims, sel->u.bb.count, data);
                init_mat(&m_var, vp->typesize, vp->ndims, vp->ldims, vp->data);

                for (i=0; i<vp->ndims; i++)
                    start[i] = MYMAX(sel->u.bb.start[i], vp->offsets[i]);

                for (i=0; i<vp->ndims; i++)
                {
                    count[i] = 
                        MYMIN(sel->u.bb.start[i]+sel->u.bb.count[i],
                              vp->offsets[i]+vp->ldims[i]) - start[i];
                }
                    
                for (i=0; i<vp->ndims; i++)
                {
                    if (count[i] <= 0)
                    {
                        log_debug("No ROI. Skip\n");
                        goto next;
                    }
                }

                for (i=0; i<vp->ndims; i++)
                    s_offsets[i] = start[i] - sel->u.bb.start[i];

                for (i=0; i<vp->ndims; i++)
                    v_offsets[i] = start[i] - vp->offsets[i];

                init_view (&v_sel, &m_sel, count, s_offsets);
                init_view (&v_var, &m_var, count, v_offsets);
                copy_view (&v_sel, &v_var);
                    
            next:
                vp = icee_varinfo_search_byname(vp->next, adiosfile->var_namelist[varid]);
            }
            break;
        }
        case ADIOS_SELECTION_AUTO:
        {
            // Possible memory overwrite
            memcpy(data, vp->data, vp->varlen);
            break;
        }
        case ADIOS_SELECTION_POINTS:
        {
            adios_error(err_operation_not_supported,
                        "ADIOS_SELECTION_POINTS not yet supported by ICEE.\n");
            break;
        }
        }

    return adios_errno;
}

int 
adios_read_icee_schedule_read(const ADIOS_FILE *adiosfile,
			const ADIOS_SELECTION * sel,
			const char * varname,
			int from_steps,
			int nsteps,
			void * data)
{
    log_error("No support yet: %s\n", __FUNCTION__);
    return 0;
}

int 
adios_read_icee_get_attr (int *gp, const char *attrname,
                                 enum ADIOS_DATATYPES *type,
                                 int *size, void **data)
{
    log_error("No support yet: %s\n", __FUNCTION__);
    return 0;
}

int 
adios_read_icee_get_attr_byid (const ADIOS_FILE *adiosfile, int attrid,
				   enum ADIOS_DATATYPES *type,
				   int *size, void **data)
{
    log_error("No support yet: %s\n", __FUNCTION__);
    return 0;
}

ADIOS_VARINFO* 
adios_read_icee_inq_var(const ADIOS_FILE * adiosfile, const char* varname)
{
    log_debug("%s (%s)\n", __FUNCTION__, varname);

    return NULL;
}

ADIOS_VARINFO* 
adios_read_icee_inq_var_byid (const ADIOS_FILE * adiosfile, int varid)
{
    log_debug("%s (%d)\n", __FUNCTION__, varid);

    icee_fileinfo_rec_ptr_t fp = (icee_fileinfo_rec_ptr_t) adiosfile->fh;
    assert(varid < fp->nvars);

    ADIOS_VARINFO *a = calloc(1, sizeof(ADIOS_VARINFO));
    
    icee_varinfo_rec_ptr_t vp = NULL;
    vp = icee_varinfo_search_byname(fp->varinfo, adiosfile->var_namelist[varid]);
    //icee_varinfo_print(vp);

    if (vp)
    {
        a->varid = vp->varid;
        a->type = vp->type;
        a->ndim = vp->ndims;

        if (vp->ndims == 0)
        {
            a->value = malloc(vp->typesize);
            memcpy(a->value, vp->data, vp->typesize);
        }
        else
        {
            uint64_t dimsize = vp->ndims * sizeof(uint64_t);
            a->dims = malloc(dimsize);
            memcpy(a->dims, vp->gdims, dimsize);
            a->global = 1;
            
            if (a->dims[0] == 0)
            {
                a->global = 0;
                memcpy(a->dims, vp->ldims, dimsize);
            }
            
            a->value = NULL;
        }
    }

    return a;
}

void 
adios_read_icee_free_varinfo (ADIOS_VARINFO *adiosvar)
{
    log_debug("%s\n", __FUNCTION__);
    free(adiosvar);
    return;
}


ADIOS_TRANSINFO* 
adios_read_icee_inq_var_transinfo(const ADIOS_FILE *gp, 
                                  const ADIOS_VARINFO *vi)
{    
    log_debug("%s\n", __FUNCTION__);
    ADIOS_TRANSINFO *trans = malloc(sizeof(ADIOS_TRANSINFO));
    memset(trans, 0, sizeof(ADIOS_TRANSINFO));
    trans->transform_type = adios_transform_none;
    return trans;
}


int 
adios_read_icee_inq_var_trans_blockinfo(const ADIOS_FILE *gp, 
                                        const ADIOS_VARINFO *vi, 
                                        ADIOS_TRANSINFO *ti)
{
    log_error("No support yet: %s\n", __FUNCTION__);
    return 0;
}

void 
adios_read_icee_reset_dimension_order (const ADIOS_FILE *adiosfile, 
                                       int is_fortran)
{
    log_error("No support yet: %s\n", __FUNCTION__);
    return;
}

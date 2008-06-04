#use this for ewok
EWOK=y
JAGUAR=n
PORTALS=n
ROHAN=n

ifeq ($(JAGUAR),y)
ifeq ($(PORTALS),y)
    USE_PORTALS=1
    PORTALS_MODULES=restart_ptl.o
    PORTALS_LIBS=-lptlclient -lIO -lportals -ldrisc -lcercs_env -ldart
else
    USE_PORTALS=0
    PORTALS_MODULES=
    PORTALS_LIBS=
endif
    #use this for cray login nodes
    #FTN=pgif90
    #CC=pgicc
    #LINK_CC=pgicc
    #MPIHOME=/opt/xt-mpt/default/mpich2-64/P2

    #use this for cray compute nodes
    FTN=ftn
    CC=cc -g
    LINK_CC=cc -g

    TARGET=-target=linux
    MPIHOME=/opt/mpich2-1.0.3

    #TARGET=-target=catamount
    #MPIHOME=/opt/xt-mpt/default/mpich2-64/P2

    LIB_HOME=$(HOME)/work/jaguar
    LIB_I=$(LIB_HOME)/include
    LIB_L=$(LIB_HOME)/lib
    MPI_LIB=-lmpich -lmpichf90

    I_PATH=-I$(MPIHOME)/include -I$(LIB_I)
ifeq ($PORTALS),y)
    I_PATH+= -I/opt/xt-pe/default/include -I/opt/xt-pe/default/cnos/linux/include
endif
    I_PATH_F=$(I_PATH)
    I_PATH_C=$(I_PATH)

    LD_PATH=-L. -L$(MPIHOME)/lib -L$(LIB_L)
ifeq ($PORTALS),y)
    LD_PATH+= -L/opt/xt-pe/default/lib -L/opt/xt-pe/default/lib_TV/snos64
endif
    LD_PATH+= -L/opt/xt-mpt/1.5.52/sma/PW/lib
    LD_PATH_F=$(LD_PATH)
    LD_PATH_C=$(LD_PATH)

    INSTALL_PREFIX=$(HOME)/work/jaguar
endif

ifeq ($(EWOK),y)
    USE_PORTALS=0
    FTN=mpif90 -fastsse
    LINK_CC=mpicc -g -fastsse
    CC=gcc -g -Wall
    PORTALS_MODULES=
    PORTALS_LIBS=
    MPIHOME_F=/usr/ofed/mpi/pgi/mvapich-0.9.7-mlx2.2.0
    MPIHOME_C=/usr/ofed/mpi/gcc/mvapich-0.9.7-mlx2.2.0
    LIB_HOME=$(HOME)/work/ewok
    LIB_I=$(LIB_HOME)/include
    LIB_L=$(LIB_HOME)/lib
    MPI_LIB=

    I_PATH_F=-I$(MPIHOME_F)/include -I$(LIB_I)
    I_PATH_C=-I$(MPIHOME_C)/include -I$(LIB_I)
    #I_PATH+= -I/opt/xt-pe/default/include -I/opt/xt-pe/default/cnos/linux/include

    LD_PATH_F=-L. -L$(MPIHOME_F)/lib -L$(LIB_L)
    LD_PATH_C=-L. -L$(MPIHOME_C)/lib -L$(LIB_L)
    #LD_PATH+= -L/opt/xt-pe/default/lib -L/opt/xt-pe/default/lib_TV/snos64
    #LD_PATH+= -L/opt/xt-mpt/1.5.52/sma/PW/lib

    INSTALL_PREFIX=$(HOME)/work/ewok
endif

# used for rohan
ifeq ($(ROHAN),y)
    USE_PORTALS=0
    FTN=mpif90
    LINK_CC=mpicc -g
    CC=icc -g -Wall
    PORTALS_MODULES=
    PORTALS_LIBS=
    MPIHOME_F=/net/hj1/ihpcl/x86_64-4AS/openmpi-1.1
    MPIHOME_C=/net/hj1/ihpcl/x86_64-4AS/openmpi-1.1
#    MPIHOME_F=/net/hj1/ihpcl/i586rh-4AS/mpich-1.2.5.2
#    MPIHOME_C= /net/hj1/ihpcl/i586rh-4AS/mpich-1.2.5.2
#    MPIHOME_F=/net/hc283/lofstead//lwfs/software/mpich2
#    MPIHOME_C= /net/hc283/lofstead//lwfs/software/mpich2
    LIB_HOME=$(HOME)/work/rohan
    LIB_I=$(LIB_HOME)/include
    LIB_L=$(LIB_HOME)/lib
    MPI_LIB=

    I_PATH_F=-I$(MPIHOME_F)/include -I$(LIB_I)
    I_PATH_C=-I$(MPIHOME_C)/include -I$(LIB_I)

    LD_PATH_F=-L$(MPIHOME_F)/lib -L$(LIB_L)
    LD_PATH_C=-L$(MPIHOME_C)/lib -L$(LIB_L)

    INSTALL_PREFIX=$(HOME)/gondor/adios
endif

MODULES_INT_ONLY=adios_transport_hooks_dummy.o
MODULES_INT=adios_internals.o bw-utils.o br-utils.o binpack-utils.o
#MODULES=adios.o adios_transport_hooks.o adios_mpi.o adios_posix.o adios_dart.o adios_datatap.o adios_posix_ascii.o adios_vtk.o $(MODULES_INT) $(PORTALS_MODULES)

MODULES=adios.o adios_transport_hooks.o adios_mpi.o adios_posix.o adios_dart.o adios_posix_ascii.o adios_vtk.o $(MODULES_INT) $(PORTALS_MODULES)
LIBS=-ladios -lmxml $(MPI_LIB) $(PORTALS_LIBS)

.SUFFIXES: .o .c .f90 .F90

# LIB TARGET
libadios.a: $(MODULES)
	rm -f libadios.a
	ar crvs libadios.a $(MODULES)
	ranlib libadios.a

libadios_int.a: $(MODULES_INT) $(MODULES_INT_ONLY)
	rm -f libadios_int.a
	ar crvs libadios_int.a $(MODULES_INT) $(MODULES_INT_ONLY)
	ranlib libadios_int.a

# FORTRAN TEST TARGET
adios_test_f: libadios.a adios_test_f.o
	$(FTN) $(TARGET) -module . $(LD_PATH_F) adios_test_f.o $(LIBS) -o adios_test_f

# C TEST TARGET
adios_test_c: libadios.a adios_test_c.o
	$(LINK_CC) $(TARGET) $(LD_PATH_C) adios_test_c.o $(LIBS) -o adios_test_c

# LINT TARGET (check the xml files for correctness)
adios_lint: adios_lint.o libadios_int.a
	$(LINK_CC) $(TARGET) $(I_PATH_C) $(LD_PATH_C) adios_lint.o -o adios_lint -ladios_int -lmxml

all: adios_test_f adios_test_c adios_lint libadios.a libadios_int.a

clean:
	rm -f *.o adios_test_c adios_test_f adios_lint libadios.a libadios_int.a restart.0

install:
	cp *.h $(INSTALL_PREFIX)/include
	cp libadios.a $(INSTALL_PREFIX)/lib
	cp libadios_int.a $(INSTALL_PREFIX)/lib

# GENERAL COMPILATION RULES
.F90.o :
	$(FTN) $(TARGET) -module . $(I_PATH_F) -c $<

.c.o :
	$(CC) $(TARGET) $(I_PATH_C) -DUSE_PORTALS=$(USE_PORTALS) -c $<

# DEPENDENCY RULES
binpack-utils.o : binpack-utils.c binpack-utils.h binpack-general.h bw-utils.h

bw-utils.o : bw-utils.c binpack-utils.h binpack-general.h bw-utils.h

br-utils.o : br-utils.c binpack-utils.h binpack-general.h br-utils.h

restart_ptl.o : restart_ptl.c restart_ptl.h

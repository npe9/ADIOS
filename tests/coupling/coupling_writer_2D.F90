!  
!  ADIOS is freely available under the terms of the BSD license described
!  in the COPYING file in the top level directory of this source distribution.
!
!  Copyright (c) 2008 - 2009.  UT-BATTELLE, LLC. All rights reserved.
!

!
!  Coupling writer/reader
!
!  Write a 2D array 
!  to/from a file or from/to another coupling code using DataSpaces/ADIOS
!
!  nx * ny          processes write a 2D array, where each process writes an
!  ndx * ndy        piece with filling with its rank as real*8 value
!  
!  Data written
!    xy         2D array with block,block decomp
!
!    All data are real*8 type
!
! (c) Oak Ridge National Laboratory, 2009
! Author: Norbert Podhorszki
!
module coupling_writer_2D_comm
    ! arguments
    character(len=256) :: filename 
    integer :: npx, npy, npz  ! # of processors in x-y-z direction
    integer :: ndx, ndy, ndz  ! size of array per processor
    integer :: timesteps      ! number of timesteps to run

    integer :: nproc          ! number of processors

    real*8, dimension(:,:),   allocatable :: xy

    ! Offsets and sizes
    integer :: offs_x, offs_y, offs_z
    integer :: dim_x_local, dim_y_local, dim_z_local
    integer :: dim_x_global, dim_y_global, dim_z_global

    ! MPI variables
    integer :: group_comm
    integer :: rank
    integer :: ierr

    ! ADIOS variables
    character (len=200) :: group
    !character (len=6)   :: nprocstr
    integer*8 :: adios_handle, adios_totalsize, adios_groupsize, adios_buf_size
    integer   :: adios_err

    ! actual timestep
    integer   :: ts 

end module coupling_writer_2D_comm


program coupling
    use coupling_writer_2D_comm
    implicit none
    include 'mpif.h'
    integer :: t

    call MPI_Init (ierr)
    call MPI_Comm_dup (MPI_COMM_WORLD, group_comm, ierr)
    call MPI_Comm_rank (MPI_COMM_WORLD, rank, ierr)
    call MPI_Comm_size (group_comm, nproc , ierr)

    call adios_init ("coupling2D_writer.xml", ierr)
    !call MPI_Barrier (group_comm, ierr)

    call processArgs()
    if (rank == 0) then
        print *, " Output file: "//trim(filename)
        print '(" Process number        : ",i0," x ",i0)',  npx,npy
        print '(" Array size per process: ",i0," x ",i0)',  ndx,ndy

        if (nproc .ne. npx*npy) then
            print '(" Error: Number of processors ",i0,"does not match N*M=",i0)', nproc, npx*npy
            call exit(1)
        endif
    endif

    !write (*,*) 'rank ', rank, "init completed"
    !write (nprocstr,'(i0)') nproc

    ! Calculate global size
    call allocateLocalArrays()
    !call sleep(60)
    do ts=1,timesteps
        call generateLocalArrays()
        call writeArrays()
        call MPI_Barrier (MPI_COMM_WORLD, ierr)
        !print '("rank=",i0," goes to sleep after step ",i0)', rank, ts
        if (ts < timesteps) call sleep(10)
        !print '("rank=",i0," woke up")', rank
    enddo

    ! Terminate
    call MPI_Barrier (MPI_COMM_WORLD, ierr)
    call adios_finalize (rank, adios_err)
    call MPI_Finalize (ierr)
end program coupling


!!***************************
subroutine allocateLocalArrays()
    use coupling_writer_2D_comm
    implicit none
    integer :: posx, posy, posz ! position index in the array
    integer :: i,j,k


!!  2D array with block,block decomposition
    posx = mod(rank, npx)     ! 1st dim easy: 0, npx, 2npx... are in the same X position
    posy = rank/npx           ! 2nd dim: npx processes belong into one dim
    offs_x = posx * ndx
    offs_y = posy * ndy
    dim_x_local   = ndx
    dim_y_local   = ndy
    dim_x_global  = npx * ndx
    dim_y_global  = npy * ndy

    print '("rank=",i0,": 2D array pos: ",i0,",",i0," offset: ",i0,",",i0)',  &
             rank, posx, posy, offs_x, offs_y

    allocate( xy(1:ndx, 1:ndy) )

end subroutine allocateLocalArrays

!!***************************
subroutine generateLocalArrays()
    use coupling_writer_2D_comm
    implicit none

    print '("rank=",i0," set matrix to ",f6.2)', rank, 1.0*rank+0.01*ts
    !xy = 1.0*rank + 0.01*ts
    xy = 1.0*rank + 1.0*ts

end subroutine generateLocalArrays

!!***************************
subroutine writeArrays()
    use coupling_writer_2D_comm
    implicit none
    character(len=256) :: fn
    ! Write out data using ADIOS

    write (fn,'(a,"_",i3.3,".bp")') trim(filename), ts
    !print '("rank=",i0," file=",A )', rank,  trim(filename)
    call adios_open (adios_handle, "writer2D", fn, "w", group_comm, adios_err)

#include "gwrite_writer2D.fh"

    ! start streaming from buffer to disk
    call adios_close (adios_handle, adios_err)
    print '("rank=",i0,": ----------------------  write completed ----------------------")', rank
end subroutine writeArrays


!!***************************
subroutine usage()
    print *, "Usage: coupling_writer_2D file N M nx ny [timesteps]"
    print *, "file:   name of file to write/read"
    print *, "N:      number of processes in X dimension"
    print *, "M:      number of processes in Y dimension"
    print *, "nx:     local array size in X dimension per processor"
    print *, "ny:     local array size in Y dimension per processor"
    print *, "timesteps: how many times to write data"
end subroutine usage

!!***************************
subroutine processArgs()
    use coupling_writer_2D_comm

#ifndef __GFORTRAN__
#ifndef __GNUC__
    interface
         integer function iargc()
         end function iargc
    end interface
#endif
#endif

    character(len=256) :: npx_str, npy_str, npz_str, ndx_str, ndy_str, ndz_str, ts_str
    integer :: numargs

    !! process arguments
    numargs = iargc()
    !print *,"Number of arguments:",numargs
    if ( numargs < 5 ) then
        call usage()
        call exit(1)
    endif
    call getarg(1, filename)
    call getarg(2, npx_str)
    call getarg(3, npy_str)
    !call getarg(4, npz_str)
    read (npx_str,'(i5)') npx
    read (npy_str,'(i5)') npy
    !read (npz_str,'(i5)') npz
    call getarg(4, ndx_str)
    call getarg(5, ndy_str)
    !call getarg(7, ndz_str)
    read (ndx_str,'(i6)') ndx
    read (ndy_str,'(i6)') ndy
    !read (ndz_str,'(i6)') ndz
    if ( numargs == 6 ) then
        call getarg(6, ts_str)
        read (ts_str,'(i6)') timesteps
    else
        timesteps = 1
    endif

end subroutine processArgs

#ifndef MYIO_H
#define MYIO_H

#include <stdio.h>
#include <stdarg.h>

#ifdef WITH_MPI
#include <mpi.h>
#endif

int printf_p( void *COMM, const char format[], ... );

#endif

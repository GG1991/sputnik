#!/bin/bash

MPIEXEC="/home/guido/libs/openmpi-install/bin/mpiexec" 

if [ $# -eq 0 ]; then
  NP=1;
else
  NP=$1;
fi

$MPIEXEC -np $NP xterm -e gdb -x file.gdb --args ../../macro/macro \
    -boundary "X0 11 0 0","X1 11 0 1" \
    -material "FIBER TYPE_0 1.0e6 1.0e6 0.3","MICRO TYPE_1" \
    -function 0.0,0.0,1.0,0.001       \
    -mesh cube_2d.msh \
    -dim 2            \
    -mesh_gmsh        \
    -eigensys         \
    -tf 1.0           \
    -dt 1.0           \
    -pc_type jacobi   \
    -ksp_type cg      \
    -print_vtu        \
    -options_left 0
#: -np 1 xterm -e gdb -x file.gdb --args ../../micro/micro \
#    -dim 2 \
#    -struct_n      15,15 \
#    -struct_l      0.1,0.1 \
#    -fiber_cilin   0.03,0.0,0.0,0.0   \
#    -mat_fiber_t0  1.0e6,1.0e7,0.3 \
#    -mat_matrix_t0 1.0e6,1.0e6,0.3 \
#    -pc_type       jacobi \
#    -ksp_type      cg \
#    -print_vtu \
#    -homo_us \
#    -options_left 0
#    -print_petsc \

#xterm -e gdb --args 
#xterm -e gdb -x file.gdb --args 
#-pc_type lu \
#-pc_type  jacobi \
#-ksp_type cg \
#-ksp_atol 1.0e-22 \
#-boundary_2 "X1 001 0 0 1"

#!/bin/bash
# 
#  Simulations to calculate the convergence of the macro-mesh 
#  with multiscale calculations
#


NM=1
Nm=1

for i in $(seq 2 20); do
  
    if ! [ -d "run_2" ]; then
      mkdir run_2
    fi
    
    if [ -d "run_2/homog_$i" ]; then
      rm -f run_2/homog_$i/*
    else
      mkdir run_2/homog_$i
    fi
    
    echo "case $i"
    ./mpirun \
    -np $NM ../../macro/macro \
    -coupl \
    -input ex2.spu \
    -mesh_gmsh \
    -mesh meshes/homoge/homog_$i.msh \
    -dim 2 \
    -pc_type lu \
    -print_vtu \
    -part_geom \
    -nr_norm_tol 1.0e-6 \
    -nr_max_its 3 \
    -tf 1.0 \
    -dt 1.0 \
    -options_left 0 \
    : \
    -np $Nm ../../micro/micro \
    -coupl \
    -input ex2.spu \
    -mesh_gmsh \
    -mesh meshes/rve/rve_5.msh \
    -dim 2 \
    -pc_type lu \
    -part_geom \
    -homo_taylor \
    -nr_norm_tol 1.0e-8 \
    -nr_max_its 3 \
    -options_left 0 > macro_$i.out

    mv macro_* run_2/homog_$i/.

    echo "ok"

done

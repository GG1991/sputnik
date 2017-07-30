#!/bin/bash

#break_mac=( 'mac_main.c:70' ) 
#break_mic=( 'mic_main.c:70' ) 
#break_mac=( 'spu_mesh.c:136' ) 
#break_mic=( 'spu_mesh.c:136' ) 
break_mac=( 'macmic.c:143' ) 
break_mic=( 'macmic.c:143' ) 
#break_mac=( 'spu_assembly.c:123' ) 
#break_mac=( 'spu_boundary.c:148' ) 

NM=2
Nm=2


# BREAKPOINTS
for i in ${break_mac[@]}
do
  exopt_mac="$exopt_mac -ex 'break $i' "
done
exopt_mac+="-ex 'r'"

for i in ${break_mic[@]}
do
  exopt_mic="$exopt_mic -ex 'break $i' "
done
exopt_mic+="-ex 'r'"

#-mat_type mpiaij 
if [ "$#" -eq 1 ];then
  if [ "$1" -eq 1 ];then  
   exec_mac="../../macro/macro ex1.spu -log_view ascii:log_summary_mac.dat"
   exec_mic="../../micro/micro ex1.spu"
   echo "./mpirun -np $NM "$exec_mac" : -np $Nm "$exec_mic""
   eval  ./mpirun -np $NM "$exec_mac" : -np $Nm "$exec_mic"
  elif [ "$1" == "-log_view" ];then  
   exec_mac="../../macro/macro ex1.spu -log_view ascii:log_summary_mac.dat"
   exec_mic="../../micro/micro ex1.spu"
   echo "./mpirun -np $NM "$exec_mac" : -np $Nm "$exec_mic""
   eval  ./mpirun -np $NM "$exec_mac" : -np $Nm "$exec_mic"
  elif [ "$1" == "-log_trace" ];then  
   exec_mac="../../macro/macro ex1.spu -log_trace macro_trace"
   exec_mic="../../micro/micro ex1.spu"
   echo "./mpirun -np $NM "$exec_mac" : -np $Nm "$exec_mic""
   eval  ./mpirun -np $NM "$exec_mac" : -np $Nm "$exec_mic"
  elif [ "$1" -eq 2 ];then
   exec_val2_mac="valgrind --log-file=\"valgrind_M.out\"  ../../macro/macro ex1.spu"
   exec_val2_mic="valgrind --log-file=\"valgrind_m.out\"  ../../micro/micro ex1.spu"
   echo "./mpirun -np $NM "$exec_val2_mac" : -np $Nm "$exec_val2_mic""
   eval  ./mpirun -np $NM "$exec_val2_mac" : -np $Nm "$exec_val2_mic" 
  elif [ "$1" -eq 3 ];then
   exec_val3_mac="valgrind --leak-check=full ../../macro/macro ex1.spu"
   exec_val3_mic="valgrind --leak-check=full ../../micro/micro ex1.spu"
   echo "./mpirun -np 2 "$exec_val3_mac" : -np 2 "$exec_val3_mic""
   eval  ./mpirun -np 2 "$exec_val3_mac" : -np 2 "$exec_val3_mic" > valgrind3-1.out 2>&1
  elif [ "$1" == "-p" ];then
   exec_print_mac="../../macro/macro ex1.spu -p -pc_type lu"
   exec_print_mic="../../micro/micro ex1.spu -p"
   echo "./mpirun -np $NM "$exec_print_mac" : -np $Nm "$exec_print_mic""
   eval  ./mpirun -np $NM "$exec_print_mac" : -np $Nm "$exec_print_mic"
  fi
else
   gdbcomm_mac="gdb $exopt_mac --args  ../../macro/macro ex1.spu"
   gdbcomm_mic="gdb $exopt_mic --args  ../../micro/micro ex1.spu"
   ./mpirun -np $NM xterm -e "$gdbcomm_mac" : -np $Nm xterm -e "$gdbcomm_mic"
fi

/*
   Common functions for <macro> & <micro> programs

   Author: Guido Giuntoli
   Date : 31-07-2017

 */

#include "micro.h"
#include "macro.h"

int macmic_coloring(MPI_Comm WORLD_COMM, int *color, coupling_t *macmic, MPI_Comm *LOCAL_COMM)
{
  /* 
     Creates the new communicators "MACRO_COMM" & "MICRO_COMM" 

     Are defined:

     id_vec        > vector of size nproc_tot that 
     id_vec[rank_wor] = MACRO|MICRO

     <--------nproc_wor-------------->

     [ 1 1 2 1 2 2 2 1 2 1 2 2 ... 2 ]

     macro_world (=1) > this communicator holds all those processes
     that belong to the same macro program are going
     to solve the micro structure in a distributed way

     micro_world (=2) > this communicator holds all those processes
     that belong to the same micro program and are going
     to solve the micro structure in a distributed way. They are joined
     in groups that are going to be coubled with macro program.

   */

  int  i, ierr, c;
  int  nproc_wor, rank_wor;
  int  nproc_mac_tot = 0, nproc_mic_tot = 0, mic_nproc_group;

  ierr = MPI_Comm_size(WORLD_COMM, &nproc_wor);
  ierr = MPI_Comm_rank(WORLD_COMM, &rank_wor);

  int  *id_vec = malloc(nproc_wor * sizeof(int));

  // Allgather of the id_vec array 
  ierr = MPI_Allgather(color,1,MPI_INT,id_vec,1,MPI_INT,WORLD_COMM);CHKERRQ(ierr);

  nproc_mic_tot = nproc_mac_tot = 0;
  for(i=0;i<nproc_wor;i++){
    if(id_vec[i] == MACRO){
      nproc_mac_tot++;
    }
    else if(id_vec[i] == MICRO){
      nproc_mic_tot++;
    }
    else{
      return 1;
    }
  }

  if(flag_coupling == true && (nproc_mic_tot==0 || nproc_mac_tot==0)){
    PetscPrintf(PETSC_COMM_WORLD,"-coupl activated but there is only one code in execution\n");
    return 1;
  }


  if(flag_coupling == false){

    // LOCAL_COMM creation
    ierr = MPI_Comm_split(WORLD_COMM, *color, 0, LOCAL_COMM);CHKERRQ(ierr);

  }
  else if(macmic->type == COUP_1){

    if(nproc_mic_tot % nproc_mac_tot != 0){
      PetscPrintf(PETSC_COMM_WORLD,"mod(nproc_mic_tot,nproc_mac_tot) = %d\n",nproc_mic_tot % nproc_mac_tot);
      return 1;
    }
    mic_nproc_group = nproc_mic_tot / nproc_mac_tot;

    int im_leader;
    int mic_pos = -1;

    if(*color == MICRO){

      // determine MICRO color 
      c = -1;
      for(i=0;i<=rank_wor;i++){
	if(id_vec[i] == MICRO){
	  mic_pos++;
	  if( mic_pos % mic_nproc_group == 0){
	    c ++; 
	  }
	}
      }
      *color += c;

      im_leader = (mic_pos % mic_nproc_group == 0) ? 1 : 0;

      // determine MACRO leaders
      int mac_rank = -1;
      i = 0;
      c = -1;
      while( i<nproc_wor ){
	if(id_vec[i] == MACRO){
	  c++;
	}
	if(c == mic_pos/mic_nproc_group){
	  mac_rank = i; 
	  break;
	}
	i++;
      }
      if(mac_rank < 0) return 1;

      macmic->coup = malloc(sizeof(mic_coup_1_t));
      ((mic_coup_1_t*)macmic->coup)->mac_rank = mac_rank;
      ((mic_coup_1_t*)macmic->coup)->im_leader = im_leader;

    } // in MICRO
    else{

      /*
	 The color is only one here (MACRO)
      */

      // determine MICRO leaders
      int mac_pos = 0;
      i = 0;
      while( i<rank_wor ){
	if(id_vec[i] == MACRO){
	  mac_pos ++;
	}
	i++;
      }

      int mic_rank = -1;
      i = 0; c = 0; mic_pos = 0;
      while( i<nproc_wor ){
	if(id_vec[i] == MICRO){
	  if(c == mac_pos){
	    mic_rank = i;
	    break;
	  }
	  if(mic_pos % mic_nproc_group == 0) c++;
	  mic_pos ++;
	}
	i++;
      }
      if(mic_rank < 0) return 1;

      macmic->coup = malloc(sizeof(mac_coup_1_t));
      ((mac_coup_1_t*)macmic->coup)->mic_rank = mic_rank;

    }

    // LOCAL_COMM creation
    ierr = MPI_Comm_split(WORLD_COMM, *color, 0, LOCAL_COMM);CHKERRQ(ierr);

  }
  else{
    return 1;
  }

  free(id_vec);

  return 0;
}
/****************************************************************************************************/
int mic_recv_signal(MPI_Comm WORLD_COMM, int *signal)
{
  /* The processes will wait here until they receive the signal */

  int ierr, remote_rank;
  MPI_Status status;

  *signal = -1;
  if(macmic.type == COUP_1){
    if(((mic_coup_1_t*)macmic.coup)->im_leader){
      remote_rank = ((mic_coup_1_t*)macmic.coup)->mac_rank;
      ierr = MPI_Recv(signal, 1, MPI_INT, remote_rank, 0, WORLD_COMM, &status);
      if(ierr)return 1;
    }
    ierr = MPI_Bcast(signal, 1, MPI_INT, 0, MICRO_COMM);
    if(ierr)return 1;
  }
  else{
    return 1;
  }
  return 0;
}
/****************************************************************************************************/
int mac_send_signal(MPI_Comm WORLD_COMM, int signal)
{
  /*
     The processes will wait here until they receive the signal
  */
  int ierr, remote_rank;
  if(macmic.type == COUP_1){
    remote_rank = ((mac_coup_1_t*)macmic.coup)->mic_rank;
    ierr = MPI_Ssend(&signal, 1, MPI_INT, remote_rank, 0, WORLD_COMM);CHKERRQ(ierr);
  }
  else{
    return 1;
  }
  return 0;
}
/****************************************************************************************************/
int mic_recv_strain(MPI_Comm WORLD_COMM, double strain[6])
{
  /* The processes will wait here until they receive a signal */

  int ierr, remote_rank;
  MPI_Status status;

  if(macmic.type == COUP_1){
    if(((mic_coup_1_t*)macmic.coup)->im_leader){
      remote_rank = ((mic_coup_1_t*)macmic.coup)->mac_rank;
      ierr = MPI_Recv(strain, 6, MPI_DOUBLE, remote_rank, 0, WORLD_COMM, &status);
      if(ierr)return 1;
    }
    ierr = MPI_Bcast(strain, 6, MPI_DOUBLE, 0, MICRO_COMM);
    if(ierr)return 1;
  }
  else{
    return 1;
  }
  return 0;
}
/****************************************************************************************************/
int mic_recv_macro_gp(MPI_Comm WORLD_COMM, int *macro_gp)
{
  /* The processes will wait here until they receive the macro_gp number */

  int ierr, remote_rank;
  MPI_Status status;

  if(macmic.type == COUP_1){
    if(((mic_coup_1_t*)macmic.coup)->im_leader){
      remote_rank = ((mic_coup_1_t*)macmic.coup)->mac_rank;
      ierr = MPI_Recv(&macro_gp, 1, MPI_INT, remote_rank, 0, WORLD_COMM, &status);
      if(ierr)return 1;
    }
    ierr = MPI_Bcast(&macro_gp, 1, MPI_INT, 0, MICRO_COMM);
    if(ierr)return 1;
  }
  else{
    return 1;
  }
  return 0;
}
/***************************************************************************************************/
int mac_send_strain(MPI_Comm WORLD_COMM, double strain[6])
{
  /* The processes will wait here until they receive the signal */

  int ierr, remote_rank;

  if(macmic.type == COUP_1){
    remote_rank = ((mac_coup_1_t*)macmic.coup)->mic_rank;
    ierr = MPI_Ssend(strain, 6, MPI_DOUBLE, remote_rank, 0, WORLD_COMM);
    if(ierr)return 1;
  }
  else{
    return 1;
  }
  return 0;
}
/***************************************************************************************************/
int mac_send_macro_gp(MPI_Comm WORLD_COMM, int *macro_gp)
{
  /* 
     sends macro_gp number to micro.
     The processes will wait here until they receive the signal 
   */

  int ierr, remote_rank;

  if(macmic.type == COUP_1){
    remote_rank = ((mac_coup_1_t*)macmic.coup)->mic_rank;
    ierr = MPI_Ssend(macro_gp, 1, MPI_INT, remote_rank, 0, WORLD_COMM);
    if(ierr)return 1;
  }
  else{
    return 1;
  }
  return 0;
}
/****************************************************************************************************/
int mic_send_stress(MPI_Comm WORLD_COMM, double stress[6])
{
  /* Sends to macro leader the averange stress tensor calculated here */

  int ierr, remote_rank;

  if(macmic.type == COUP_1){
    if(((mic_coup_1_t*)macmic.coup)->im_leader){
      // only the micro leader sends the stress
      remote_rank = ((mic_coup_1_t*)macmic.coup)->mac_rank;
      ierr = MPI_Ssend(stress, 6, MPI_DOUBLE, remote_rank, 0, WORLD_COMM); 
      if(ierr)return 1; 
    }
  }
  else{
    return 1;
  }
  return 0;
}
/****************************************************************************************************/
int mic_send_strain(MPI_Comm WORLD_COMM, double strain[6])
{
  /* Sends to macro leader the averange strain tensor calculated here */
  int ierr, remote_rank;

  if(macmic.type == COUP_1){
    if(((mic_coup_1_t*)macmic.coup)->im_leader){
      // only the micro leader sends the stress
      remote_rank = ((mic_coup_1_t*)macmic.coup)->mac_rank;
      ierr = MPI_Ssend(strain, 6, MPI_DOUBLE, remote_rank, 0, WORLD_COMM); 
      if(ierr)return 1;
    }
  }
  else{
    return 1;
  }
  return 0;
}
/****************************************************************************************************/
int mac_recv_stress(MPI_Comm WORLD_COMM, double stress[6])
{
  /* The processes will wait here until they receive the stress */
  int ierr, remote_rank;
  MPI_Status status;

  if(macmic.type==COUP_1){
    remote_rank = ((mac_coup_1_t*)macmic.coup)->mic_rank;
    ierr = MPI_Recv(stress, 6, MPI_DOUBLE, remote_rank, 0, WORLD_COMM, &status); 
    if(ierr)return 1;
  }
  else{
    return 1;
  }
  return 0;
}
/****************************************************************************************************/
int mac_recv_rho(MPI_Comm WORLD_COMM, double *rho)
{
  /* The processes will wait here until they receive the stress */
  int ierr, remote_rank;
  MPI_Status status;

  if(macmic.type==COUP_1){
    remote_rank = ((mac_coup_1_t*)macmic.coup)->mic_rank;
    ierr = MPI_Recv(rho, 1, MPI_DOUBLE, remote_rank, 0, WORLD_COMM, &status); 
    if(ierr) return 1;
  }
  else{
    return 1;
  }
  return 0;
}
/****************************************************************************************************/
int mac_recv_c_homo(MPI_Comm WORLD_COMM, double c_homo[36])
{
  /* The processes will wait here until they receive the c_homo */

  int        ierr;
  int        remote_rank;
  MPI_Status status;

  if(macmic.type==COUP_1){
    remote_rank = ((mac_coup_1_t*)macmic.coup)->mic_rank;
    ierr = MPI_Recv(c_homo, nvoi*nvoi, MPI_DOUBLE, remote_rank, 0, WORLD_COMM, &status);
    if(ierr)return 1; 
  }
  else{
    return 1;
  }
  return 0;
}
/****************************************************************************************************/
int mic_send_c_homo(MPI_Comm WORLD_COMM, double c_homo[36])
{
  /* Sends to macro leader the C homogenized tensor */

  int ierr, remote_rank;

  if(macmic.type == COUP_1){
    if(((mic_coup_1_t*)macmic.coup)->im_leader){
      // only the micro leader sends the tensor
      remote_rank = ((mic_coup_1_t*)macmic.coup)->mac_rank;
      ierr = MPI_Ssend(c_homo, nvoi*nvoi, MPI_DOUBLE, remote_rank, 0, WORLD_COMM);
      if(ierr)return 1;
    }
  }
  else{
    return 1;
  }
  return 0;
}
/****************************************************************************************************/

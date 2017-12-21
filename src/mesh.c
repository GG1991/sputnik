#include "sputnik.h"
#include "parmetis.h"
#include "mesh.h"

#define NBUF 256

int mesh_fill_boundary_list_from_command_line(command_line_t *command_line, list_t *boundary_list){

  bool found;
  int num_string_found;
  char string_arr[MAX_NUM_OF_BOUNDARIES][128];

  myio_comm_line_get_string_array(command_line, "-boundary", string_arr, MAX_NUM_OF_BOUNDARIES, &num_string_found, &found);

  if(found == false || num_string_found == 0)
    return 1;

  list_init(boundary_list, sizeof(mesh_boundary_t), NULL);

  for(int i = 0 ; i < num_string_found ; i++){

    char *str_token = strtok(string_arr[i]," \n");

    mesh_boundary_t bou;
    bou.name = strdup(str_token);
    str_token = strtok(NULL," \n");
    bou.kind = strbin2dec(str_token);

    if(dim == 2){
      if(bou.kind == 1 || bou.kind == 2) bou.ndirpn = 1;
      if(bou.kind == 3) bou.ndirpn =  2;
      if(bou.kind == 0) bou.ndirpn =  0;
    }

    bou.nneupn   = dim - bou.ndirpn;
    bou.fnum     = malloc(dim * sizeof(int));

    for(int d = 0 ; d < dim ; d++){
      str_token = strtok(NULL," \n");
      bou.fnum[d] = atoi(str_token);
    }
    bou.dir_loc_ixs = NULL;
    bou.dir_val = NULL;
    list_insertlast(boundary_list, &bou);
  }

  return 0;
}


int mesh_do_partition(MPI_Comm COMM, mesh_t *mesh){

  int rank, nproc, ierr;
  MPI_Comm_size(COMM, &nproc);
  MPI_Comm_rank(COMM, &rank);

  if(nproc == 1) return 0;

  int *part = malloc(mesh->nelm_local * sizeof(int));

  if(mesh->partition == PARMETIS_GEOM || mesh->partition == PARMETIS_MESHKWAY){

#ifdef PARTMETIS

    idx_t *elmwgt;
    idx_t wgtflag;
    idx_t numflag;
    idx_t ncon;
    idx_t ncommonnodes;
    idx_t nparts;
    real_t *tpwgts;
    real_t *ubvec;
    idx_t options[3];
    idx_t edgecut;

    elmwgt = NULL;
    wgtflag = 0;
    numflag = 0;
    nparts = nproc;
    ncon = 1;
    tpwgts = malloc(ncon*nparts*sizeof(real_t));

    for(int i = 0 ; i < ncon*nparts ; i++)
      tpwgts[i] = 1.0 / nparts;

    ncommonnodes = 3;

    options[0] = 0;
    options[1] = 0;
    options[2] = 0;

    ubvec = malloc( ncon * sizeof(real_t) );
    for(int i = 0 ; i < ncon ; i++)
      ubvec[i] = 1.05;

    if(mesh->partition == PARMETIS_GEOM){

      ierr = ParMETIS_V3_PartGeom( elmdist, &dim, (real_t*)elmv_centroid, part, &COMM );

    }else if(mesh->partition == PARMETIS_MESHKWAY){

      ierr = ParMETIS_V3_PartMeshKway(mesh->elm_dist, mesh->eptr, mesh->eind, elmwgt, &wgtflag, &numflag,
	  &ncon, &ncommonnodes, &nparts, tpwgts, ubvec, options, &edgecut, part, &COMM);
    }
    free(ubvec);
    free(tpwgts);

#else
    return 1;
#endif
  }

  int *eind_swi = malloc(mesh->eptr[mesh->nelm_local]*sizeof(int));
  int *npe_swi = malloc(mesh->nelm_local*sizeof(int));
  int *elm_id_swi = malloc(mesh->nelm_local*sizeof(int));
  int *eind_swi_size = malloc(nproc*sizeof(int));
  int *npe_swi_size = malloc(nproc*sizeof(int));
  int *eind_size_new = malloc(nproc*sizeof(int));
  int *npe_size_new = malloc(nproc*sizeof(int));

  ierr = swap_vectors_SCR(part, nproc, mesh->nelm_local,
      mesh->npe, mesh->eptr, mesh->eind, mesh->elm_id,
      npe_swi, eind_swi, elm_id_swi,
      npe_swi_size, eind_swi_size );

  ierr = MPI_Alltoall(npe_swi_size, 1, MPI_INT, npe_size_new, 1, MPI_INT, COMM);
  if(ierr != 0)
    return 1;

  ierr = MPI_Alltoall(eind_swi_size, 1, MPI_INT, eind_size_new, 1, MPI_INT, COMM);
  if(ierr != 0)
    return 1;

  int npe_size_new_tot = 0, eind_size_new_tot = 0;

  for(int i = 0 ; i < nproc ; i++){
    npe_size_new_tot += npe_size_new[i];
    eind_size_new_tot += eind_size_new[i];
  }
  mesh->nelm_local = npe_size_new_tot;

  free(mesh->npe);
  free(mesh->eptr);
  free(mesh->eind);
  free(mesh->elm_id);
  free(part);

  mesh->npe = malloc(mesh->nelm_local*sizeof(int));
  mesh->eptr = malloc((mesh->nelm_local + 1)*sizeof(int));
  mesh->eind = malloc(eind_size_new_tot*sizeof(int));
  mesh->elm_id = malloc(mesh->nelm_local*sizeof(int));

  int *sdispls = malloc(nproc*sizeof(int));
  int *rdispls = malloc(nproc*sizeof(int));

  for(int i = 0 ; i < nproc ; i++){
    sdispls[i] = 0;
    for(int j = 0 ; j < i ; j++)
      sdispls[i] += npe_swi_size[j];
  }
  for(int i = 0 ; i < nproc ; i++){
    rdispls[i] = 0;
    for(int j = 0 ; j < i ; j++)
      rdispls[i] += npe_size_new[j];
  }

  ierr = MPI_Alltoallv(npe_swi, npe_swi_size, sdispls, MPI_INT,
      mesh->npe, npe_size_new, rdispls, MPI_INT, COMM);
  if(ierr != 0)
    return ierr;

  ierr = MPI_Alltoallv(elm_id_swi, npe_swi_size, sdispls, MPI_INT,
      mesh->elm_id, npe_size_new, rdispls, MPI_INT, COMM);
  if(ierr != 0)
    return ierr;

  mesh->eptr[0] = 0;
  for(int i = 0 ; i < mesh->nelm_local ; i++)
    mesh->eptr[i+1] = mesh->eptr[i] + mesh->npe[i];

  for(int i = 0 ; i < nproc ; i++){
    sdispls[i] = 0;
    for(int j = 0 ; j < i ; j++)
      sdispls[i] += eind_swi_size[j];
  }
  for(int i = 0 ; i < nproc ; i++){
    rdispls[i] = 0;
    for(int j = 0 ; j < i ; j++)
      rdispls[i] += eind_size_new[j];
  }

  ierr = MPI_Alltoallv(eind_swi, eind_swi_size, sdispls, MPI_INT,
      mesh->eind, eind_size_new, rdispls, MPI_INT, COMM);

  free(eind_swi);
  free(npe_swi);
  free(npe_swi_size);
  free(npe_size_new);
  free(eind_swi_size);
  free(eind_size_new);
  free(sdispls);
  free(rdispls);
  free(elm_id_swi);

  clean_vector_qsort(mesh->eptr[mesh->nelm_local], mesh->eind, &mesh->local_ghost_nods, &mesh->nnods_local_ghost);

  return 0;
}


int swap_vector(int *swap, int n, int *vector, int *new_vector, int *cuts){

  /*
     swap       = [ 0 1 0 0 1 2 2 ]
     vector     = [ 0 1 2 3 4 5 6 ]
     new_vector = [ 0 2 3 1 4 5 6 ]
     cut        = [ 3 2 2 ]
   */

  int *aux_vector;

  if(n == 0) return 0;

  if(vector == NULL || cuts == NULL) return 1;

  if(new_vector == NULL)
    aux_vector = vector;
  else
    aux_vector = new_vector;

  int j = 0;
  for(int p = 0 ; p < n ; p++ ){
    cuts[p] = 0;
    for(int i = 0 ; i < n ; i++ ){
      if(swap[i] == p){
	int aux = vector[i];
	aux_vector[i] = vector[j];
	aux_vector[j] = aux;
	j++;
	cuts[p] ++;
      }
    }
  }
  return 0;
}


int swap_vectors_SCR(int *swap, int nproc, int n,  int *npe,
    int *eptr, int *eind, int *elm_id,
    int *npe_swi, int *eind_swi, int *elm_id_swi,
    int *npe_size, int *eind_size){

  /*
     swap        = [ 0 2 1 0 ] (swap will be generally the "part" array)
     npe         = [ 3 2 3 1 ]
     elm_id  = [ 0 0 1 2 ]
     eind        = [ 3 2 0 | 1 2 | 1 0 1 |3 ]

     npe_swi     = [ 3 1 3 2 ]
     elm_id  = [ 0 2 1 0 ]
     eind_swi    = [ 3 2 0 | 3 | 1 0 1 | 1 2 ]
   */

  int lp, pi, c;

  if(n == 0) return 0;

  if(!npe || !eind || !elm_id ||
      !eind_swi || !npe_swi || !elm_id_swi ||
      !npe_size || !eind_size){
    return 1;
  }

  int j = pi = lp = 0;
  for(int p = 0 ; p < nproc ; p++){

    npe_size[p] = 0;
    for(int e = 0 ; e < n ; e++){

      if(swap[e] == p){

	// swap npe
	npe_swi[j] = npe[e];
	elm_id_swi[j] = elm_id[e];
	j ++;

	// swap eind
	pi = eptr[e];

	for(int i = 0 ; i < npe[e] ; i++){
	  eind_swi[lp] = eind[ pi + i ];
	  lp ++;
	}
	npe_size[p] ++;
      }
    }
  }

  c = 0;
  for(int i = 0 ; i < nproc ; i++){
    eind_size[i] = 0;
    for(j = 0 ; j < npe_size[i] ; j++){
      eind_size[i] += npe_swi[c];
      c++;
    }
  }

  return 0;
}


int clean_vector_qsort(int n, int *input, int **output, int *n_notrep){

  int  swi, val_o, *aux = NULL;

  if(n == 0) return 0;
  if(*output) return 1;

  aux = malloc(n*sizeof(int));
  for(int i = 0 ; i < n ; i++){
    aux[i] = input[i];
  }

  qsort(aux, n, sizeof(int), cmpfunc);

  val_o = aux[0];
  int c = 1;
  for(int i = 1 ; i < n ; i++){
    swi = 1;
    if(aux[i] == val_o){
      swi = 0;
    }
    else{
      val_o = aux[i];
      swi = 1;
    }
    if(swi==1){
      c++;
    }
  }
  (*output) = malloc(c*sizeof(int));

  val_o = aux[0];
  (*output)[0] = aux[0];
  c = 1;
  for(int i = 1 ; i < n ; i++){
    swi = 1;
    if(aux[i] == val_o){
      swi = 0;
    }
    else{
      val_o = aux[i];
      swi = 1;
    }
    if(swi==1){
      (*output)[c] = aux[i];
      c ++;
    }
  }
  free(aux);
  *n_notrep = c;

  return 0;
}


int give_repvector_qsort(MPI_Comm * comm, char *myname, int n, int *input, int **output, int *nrep){

  int   i, c, swi, val_o;
  int   *aux;
  int   rank;

  MPI_Comm_rank(*comm, &rank);

  (*nrep) = 0;

  aux = malloc(n*sizeof(int));
  memcpy(aux, input, n*sizeof(int));

  qsort(aux, n, sizeof(int), cmpfunc);

  val_o = aux[0];
  swi = 1;
  for(i=1;i<n;i++){
    if(aux[i] == val_o){
      if(swi == 1){
	(*nrep) ++;
	swi = 0;
      }
    }
    else if(aux[i] != val_o){
      swi = 1;
    }
    val_o = aux[i];
  }
  (*output) = malloc( (*nrep) * sizeof(int));

  c = 0;
  val_o = aux[0];
  swi = 1;
  for(i=1;i<n;i++){
    if(aux[i] == val_o){
      if(swi == 1){
	(*output)[c] = val_o;
	c ++;
	swi = 0;
      }
    }
    else if(aux[i] != val_o){
      swi = 1;
    }
    val_o = aux[i];
  }

  free(aux);

  return 0;
}


int vector_intersection(int *array1, int n1, int *array2, int n2, int **reps, int *nreps){

  int i, j, c;

  i = j = *nreps = 0;
  while( i < n2 && j < n1  ){
    if( array1[j] < array2[i] ){
      j ++;
    }
    else if( array1[j] > array2[i] ){
      i ++;
    }
    else if( array1[j] == array2[i] ){
      j ++;
      i ++;
      (*nreps) ++;
    }
  }
  *reps = malloc((*nreps) * sizeof(int));

  i = j = c = 0;
  while( i < n2 && j < n1  ){
    if( array1[j] < array2[i] ){
      j ++;
    }
    else if( array1[j] > array2[i] ){
      i ++;
    }
    else if( array1[j] == array2[i] ){
      (*reps)[c] = array2[i];
      j ++;
      i ++;
      c ++;
    }
  }

  return 0;
}


int mesh_calc_local_and_ghost(MPI_Comm COMM, mesh_t *mesh){

  int ierr;

  int *peer_sizes, mysize, *peer_nod_glo;
  int **rep_matrix, *nrep;

  MPI_Request *request;

  int  rank, nproc;
  MPI_Comm_rank( COMM, &rank);
  MPI_Comm_size( COMM, &nproc);

  mysize = mesh->nnods_local_ghost;
  peer_sizes = NULL;

  peer_sizes = malloc(nproc*sizeof(int));
  request = malloc(nproc*sizeof(MPI_Request));
  rep_matrix = calloc(nproc,sizeof(int*) );
  nrep = calloc(nproc,sizeof(int) );

  ierr = MPI_Allgather(&mysize, 1, MPI_INT, peer_sizes, 1, MPI_INT, COMM);

  for(int i = 0 ; i < nproc ; i++){
    if(i != rank){
      ierr = MPI_Isend(mesh->local_ghost_nods, mesh->nnods_local_ghost, MPI_INT, i, 0, COMM, &request[i]); 
      if(ierr != 0) return 1;
    }
  }
  for(int i = 0 ; i < nproc ; i++){
    if(i != rank){
      peer_nod_glo = malloc(peer_sizes[i]*sizeof(int));
      ierr = MPI_Recv(peer_nod_glo, peer_sizes[i], MPI_INT, i, 0, COMM, MPI_STATUS_IGNORE );
      vector_intersection(mesh->local_ghost_nods, mesh->nnods_local_ghost, peer_nod_glo, peer_sizes[i], &rep_matrix[i], &nrep[i]);
      free(peer_nod_glo);
    }
  }

  int nreptot = 0;
  for(int i = 0 ; i < nproc ; i++){
    if(i != rank)
      nreptot += nrep[i];
  }
  int *rep_array = malloc(nreptot*sizeof(int));

  int c = 0;
  for(int i = 0 ; i < nproc ; i++){
    if(int i != rank){
      for(int j = 0 ; j < nrep[i] ; j++)
	rep_array[c++] = rep_matrix[i][j];
    }
  }

  int *rep_array_clean = NULL, nreptot_clean = 0;
  ierr = clean_vector_qsort(nreptot, rep_array, &rep_array_clean, &nreptot_clean);
  free(rep_array);

  int rep_count = 0, remote_rank;

  if(nreptot_clean != 0){

    mesh->nnods_local = 0;
    mesh->nnods_ghost = 0;
    for(int i = 0 ; i < mesh->nnods_local_ghost ; i++){

      if(rep_count < nreptot_clean){

	if(mesh->local_ghost_nods[i] == rep_array_clean[rep_count]){

	  int ismine = ownership_selec_rule(COMM, rep_matrix, nrep, allnods[i], &remote_rank);
	  if(ismine != 1)
	    mesh->nnods_local++;
	  else
	    mesh->nnods_ghost++;

	  rep_count++;

	}else
	  mesh->nnods_local++;
      }
      else
	mesh->nnods_local++;
    }
  }else{
    mesh->nnods_local = mesh->nnods_local_ghost;
    mesh->nnods_ghost = 0;
  }

  mesh->local_nods = malloc(mesh->nnods_local*sizeof(int));
  mesh->ghost_nods = malloc(mesh->nnods_ghost*sizeof(int));

  int local_count = 0; int ghost_count = 0; rep_count = 0;
  if(nreptot_clean != 0){

    for(int i = 0 ; i < mesh->nnods_local_ghost ; i++){

      if(rep_count < nreptot_clean){

	if(mesh->local_ghost_nods[i] == rep_array_clean[rep_count]){

	  int ismine = ownership_selec_rule(COMM, rep_matrix, nrep, mesh->local_ghost_nods[i], &remote_rank);

	  if(ismine)
	    mesh->local_nods[local_count+] = mesh->local_ghost_nods[i];
	  else
	    mesh->ghost_nods[ghost_count++] = mesh->local_ghost_nods[i];

	  rep_count++;

	}else
	  mesh->local_nods[local_count++] = mesh->local_ghost_nods[i];

      }else
	mesh->local_nods[local_count++] = mesh->local_ghost_nods[i];
    }

  }else{
    for(int i = 0 ;i < nallnods ; i++)
      mesh->local_nods[i] = mesh->local_ghost_nods[i];
  }


  for(int i = 0 ; i < nproc ; i++ ){
    if(i != rank)
      free(rep_matrix[i]);
  }
  free(rep_matrix);
  free(nrep);
  free(request);
  free(peer_sizes);

  return 0;
}


int reenumerate_PETSc(MPI_Comm COMM){

  int i, j, *p, ierr;
  int *rem_nods;

  int  rank, nproc;
  MPI_Comm_rank( COMM, &rank );
  MPI_Comm_size( COMM, &nproc );

  int *rem_nnod  = malloc( nproc * sizeof(int) );
  int *disp_nods = malloc( nproc * sizeof(int) );
  ierr = MPI_Allgather( &nmynods, 1, MPI_INT, rem_nnod, 1, MPI_INT, COMM ); if( ierr ) return 1;

  disp_nods[0] = 0;
  i = 1;
  while( i < nproc ){
    disp_nods[i] = disp_nods[i-1] + rem_nnod[i-1];
    i++;
  }

  /*  reenumeramos "eind" */
  for( i = 0 ; i < eptr[nelm] ; i++ ){
    p = bsearch( &eind[i], mynods, nmynods, sizeof(int), cmpfunc );
    if( p != NULL ){
      // is a local node
      eind[i] = p - mynods;
    }
    else
    {
      // is a ghost node
      p = bsearch( &eind[i], ghost, nghost, sizeof(int), cmpfunc );
      if( p != NULL )
	eind[i] = nmynods + p - ghost;
      else{
	PetscPrintf( COMM, "\nvalue %d not found on <mynods> neither <ghost>\n", eind[i] );
	return 1;
      }
    }
  }

  loc2petsc = malloc( (nmynods + nghost) * sizeof(int));

  for( i = 0 ; i < nmynods ; i++ )
    loc2petsc[i] = disp_nods[rank] + i;

  MPI_Request  *request;

  int   *ghost_gix; // ghost global index

  request = malloc( nproc * sizeof(MPI_Request) );
  ghost_gix = malloc( nghost * sizeof(int) );
  for( i = 0 ; i < nghost ; i++ )
    ghost_gix[i] = -1;

  for( i = 0 ; i < nproc ; i++ ){
    if( i != rank ){
      ierr = MPI_Isend( mynods, nmynods, MPI_INT, i, 0, COMM, &request[i] );
      if( ierr ) return 1;
    }
  }
  for( i = 0 ; i < nproc ; i++ )
  {
    // receive from all peer ranks "i"
    if( i != rank )
    {
      rem_nods = malloc(rem_nnod[i]*sizeof(int));

      ierr = MPI_Recv(rem_nods, rem_nnod[i], MPI_INT, i, 0, COMM, MPI_STATUS_IGNORE );
      if( ierr ) return 1;

      for( j = 0 ; j < nghost ; j++ )
      {
	// search this ghost node on <rem_nods>
	p = bsearch( &ghost[j], rem_nods, rem_nnod[i], sizeof(int), cmpfunc );
	if( p != NULL )
	  ghost_gix[j] = disp_nods[i] + p - rem_nods;
      }
      free(rem_nods);
    }
  }

  // check if all the ghost where found remotely
  for( i = 0 ; i < nghost ; i++ ){
    if( ghost_gix[i] == -1 ){
      PetscPrintf( COMM,"\n\"ghost\" value %d not found remotely", ghost[i] );
      return 1;
    }
  }

  for( i = 0 ; i < nghost ; i++ )
    loc2petsc[nmynods + i] =  ghost_gix[i];

  free(rem_nnod);
  free(ghost_gix);
  free(request);
  return 0;
}


int ownership_selec_rule(MPI_Comm COMM, int **rep_matrix, int *nrep, int node, int *remote_rank){

  /*
      Function for determine the ownership of a rep_matrix
      node on different processors.

      Input

      rep_matrix > list of nodes that each process have in common with me
      nrep     > number of elements in each <rep_matrix> element
      node     > node numeration in order to know if this process owns it

      Notes>
      -> all process should return the same if <node> is the same
      -> the selection criteria calculates rankp = node % nproc as root
      if the rankp in rep_matrix contains <node> in <rankp> position
      then this is the ownership of it. If <rankp> = <rank> at any
      part of the search then this node is of this process.
   */

  int nproc, rank;

  MPI_Comm_rank( COMM, &rank);
  MPI_Comm_size( COMM, &nproc);

  int i, rankp;

  // damos un guess inicial de <rankp> luego iremos buscamos
  // hacia los ranks crecientes
  rankp = node % nproc;

  i = 0;
  while(i<nproc){
    //tenemos un guess nuevo de rankp
    if(rankp == rank){
      // si justo nos cayo entonces este <node> es nuestro
      *remote_rank = rankp;
      return 1;
    }
    else{
      if(is_in_vector( node, &rep_matrix[rankp][0], nrep[rankp])){
	// lo encontramos pero está en otro rank
	*remote_rank = rankp;
	return 0;
      }
      else{
	// buscamos siempre a la derecha
	rankp ++;
	if( rankp == nproc ) rankp = 0;
      }
    }
    i ++;
  }

  return -1;	
}


int is_in_vector(int val, int *vector, int size){

  int j = 0;
  while(j < size){
    if(vector[j] == val) break;
    j++;
  }
  return (j == size) ? 0 : 1;

  return -1;
}


int get_bbox_local_limits(double *coord, int n, double *x, double *y, double *z){

  if( n == 0 ) return 0;

  int i=0;
  x[0] = coord[i*dim+0]; x[1] = coord[i*dim+0];
  y[0] = coord[i*dim+1]; y[1] = coord[i*dim+1];
  if(dim==2){
    z[0] = 0.0; z[1] = 0.0;
  }
  else if(dim==3){
    z[0] = coord[i*dim+2]; z[1] = coord[i*dim+2];
  }
  for(i=1;i<n;i++){
    if( coord[i*dim+0] < x[0] ) x[0] = coord[i*dim+0];
    if( coord[i*dim+0] > x[1] ) x[1] = coord[i*dim+0];
    if( coord[i*dim+1] < y[0] ) y[0] = coord[i*dim+1];
    if( coord[i*dim+1] > y[1] ) y[1] = coord[i*dim+1];
    if(dim==3){
      if( coord[i*dim+2] < z[0] ) z[0] = coord[i*dim+2];
      if( coord[i*dim+2] > z[1] ) z[1] = coord[i*dim+2];
    }
  }

  return 0;
}


int get_domain_center(MPI_Comm PROBLEM_COMM, double *coord, int n, double center[3])
{
  int    rank, nproc, ierr, i;
  double x[2],y[2],z[2],x_abs[2],y_abs[2],z_abs[2],*x_all,*y_all,*z_all;

  MPI_Comm_size(PROBLEM_COMM, &nproc);
  MPI_Comm_rank(PROBLEM_COMM, &rank);

  x_all = malloc(nproc*2*sizeof(double));
  y_all = malloc(nproc*2*sizeof(double));
  z_all = malloc(nproc*2*sizeof(double));

  ierr = get_bbox_local_limits(coord, n, x, y, z);CHKERRQ(ierr);

  ierr = MPI_Allgather(x, 2, MPI_DOUBLE, x_all, 2, MPI_DOUBLE, PROBLEM_COMM);CHKERRQ(ierr);
  ierr = MPI_Allgather(y, 2, MPI_DOUBLE, y_all, 2, MPI_DOUBLE, PROBLEM_COMM);CHKERRQ(ierr);
  ierr = MPI_Allgather(z, 2, MPI_DOUBLE, z_all, 2, MPI_DOUBLE, PROBLEM_COMM);CHKERRQ(ierr);

  x_abs[0]=x_all[0]; x_abs[1]=x_all[1];
  y_abs[0]=y_all[0]; y_abs[1]=y_all[1];
  z_abs[0]=z_all[0]; z_abs[1]=z_all[1];
  for(i=1;i<nproc;i++){
    if( x_all[2*i+0] < x_abs[0] ) x_abs[0] = x_all[2*i+0];
    if( x_all[2*i+1] > x_abs[1] ) x_abs[1] = x_all[2*i+1];
    if( y_all[2*i+0] < y_abs[0] ) y_abs[0] = y_all[2*i+0];
    if( y_all[2*i+1] > y_abs[1] ) y_abs[1] = y_all[2*i+1];
    if( z_all[2*i+0] < z_abs[0] ) z_abs[0] = z_all[2*i+0];
    if( z_all[2*i+1] > z_abs[1] ) z_abs[1] = z_all[2*i+1];
  }

  center[0] = (x_abs[1] + x_abs[0])/ 2;
  center[1] = (y_abs[1] + y_abs[0]) /2;
  center[2] = (z_abs[1] + z_abs[0]) /2;
  return 0;
}


int get_bbox_limit_lengths(MPI_Comm PROBLEM_COMM, double *coord, int n, double *lx, double *ly, double *lz){

  int rank, nproc, ierr, i;
  double x[2],y[2],z[2],x_abs[2],y_abs[2],z_abs[2],*x_all,*y_all,*z_all;

  MPI_Comm_size(PROBLEM_COMM, &nproc);
  MPI_Comm_rank(PROBLEM_COMM, &rank);

  x_all = malloc(nproc*2*sizeof(double));
  y_all = malloc(nproc*2*sizeof(double));
  z_all = malloc(nproc*2*sizeof(double));

  ierr = get_bbox_local_limits(coord, n, x, y, z);CHKERRQ(ierr);

  ierr = MPI_Allgather(x, 2, MPI_DOUBLE, x_all, 2, MPI_DOUBLE, PROBLEM_COMM);CHKERRQ(ierr);
  ierr = MPI_Allgather(y, 2, MPI_DOUBLE, y_all, 2, MPI_DOUBLE, PROBLEM_COMM);CHKERRQ(ierr);
  ierr = MPI_Allgather(z, 2, MPI_DOUBLE, z_all, 2, MPI_DOUBLE, PROBLEM_COMM);CHKERRQ(ierr);

  x_abs[0]=x_all[0]; x_abs[1]=x_all[1];
  y_abs[0]=y_all[0]; y_abs[1]=y_all[1];
  z_abs[0]=z_all[0]; z_abs[1]=z_all[1];
  for(i=1;i<nproc;i++){
    if( x_all[2*i+0] < x_abs[0] ) x_abs[0] = x_all[2*i+0];
    if( x_all[2*i+1] > x_abs[1] ) x_abs[1] = x_all[2*i+1];
    if( y_all[2*i+0] < y_abs[0] ) y_abs[0] = y_all[2*i+0];
    if( y_all[2*i+1] > y_abs[1] ) y_abs[1] = y_all[2*i+1];
    if( z_all[2*i+0] < z_abs[0] ) z_abs[0] = z_all[2*i+0];
    if( z_all[2*i+1] > z_abs[1] ) z_abs[1] = z_all[2*i+1];
  }

  *lx = x_abs[1] - x_abs[0];
  *ly = y_abs[1] - y_abs[0];
  *lz = z_abs[1] - z_abs[0];

  free(x_all);
  free(y_all);
  free(z_all);
  return 0;
}


int build_structured_2d(int **eind, int **eptr, double **coor, double limit[4], int nx, int ny){

  double   x0 = limit[0];
  double   x1 = limit[1];
  double   y0 = limit[2];
  double   y1 = limit[3];
  int      nnod = nx*ny;
  int      nex = (nx-1);
  int      ney = (ny-1);
  double   dx = (x1-x0)/nex;
  double   dy = (y1-y0)/ney;
  int      nelm = nex*ney;
  int      npe = 4;
  int      i, j, e, n;

  *eind = malloc(nelm*npe*sizeof(int));
  *eptr = malloc((nelm+1)*sizeof(int));
  *coor = malloc(nnod*2*sizeof(double));

  for(i=0;i<nex;i++){
    for(j=0;j<ney;j++){
      e = i*nex + j;
      (*eptr)[e+0]     = ( e + 0 )*npe;
      (*eptr)[e+1]     = ( e + 1 )*npe;
      (*eind)[e*npe+0] = j   + i*nx;
      (*eind)[e*npe+1] = j+1 + i*nx;
      (*eind)[e*npe+2] = j+1 + (i+1)*nx;
      (*eind)[e*npe+3] = j   + (i+1)*nx;
    }
  }
  for(i=0;i<nx;i++){
    for(j=0;j<ny;j++){
      n = i*nx + j;
      (*coor)[n*2+0] = x0 + dx*j;
      (*coor)[n*2+1] = y0 + dy*i;
    }
  }

  return 0;
}


//int interpolate_structured_2d(double limit[2], int nx, int ny, double *field, double *var_interp)
//{
//  /*
//     <field> size should be <nelm>
//  */
//  int      e, es;
//  double   x0 = limit[0];
//  double   x1 = limit[1];
//  double   y0 = limit[2];
//  double   y1 = limit[3];
//  int      nex = (nx-1);
//  int      ney = (ny-1);
//  int      nelm_s = nex*ney;
//  double   dx = (x1-x0)/nex;
//  double   dy = (y1-y0)/ney;
//  double   centroid[2];
//  double   vol;
//  double   *var_interp_struct;
//
//  var_interp_struct = calloc(nelm_s,sizeof(double));
//
//  for(e=0;e<nelm;e++){
//    get_centroid(e, centroid);
//    get_element_structured_2d(centroid, limit, nx, ny, &es);
//    get_elem_vol(e, &vol);
//    var_interp_struct[es] = var_interp_struct[es] + field[e]*vol;
//  }
//  for(e=0;e<nelm;e++){
//    get_centroid(e, centroid);
//    get_element_structured_2d(centroid, limit, nx, ny, &es);
//    var_interp[e] = var_interp_struct[es] / (dx*dy);
//  }
//
//  return 0;
//}


int get_element_structured_2d(double centroid[2], double limit[4], int nx, int ny, int *es){

  double   x0 = limit[0];
  double   x1 = limit[1];
  double   y0 = limit[2];
  double   y1 = limit[3];
  int      nex = (nx-1);
  int      ney = (ny-1);
  int      i, j;
  double   dx = (x1-x0)/nex;
  double   dy = (y1-y0)/ney;
  double   x_min;
  double   x_max;
  double   y_min;
  double   y_max;

  j=0;
  while(j<nex){
    x_min = x0 + dx*j;
    x_max = x0 + dx*(j+1);
    if(x_min < centroid[0] && centroid[0] < x_max){
      break;
    }
    j++;
  }

  i=0;
  while(i<ney){
    y_min = y0 + dy*i;
    y_max = y0 + dy*(i+1);
    if(y_min < centroid[1] && centroid[1] < y_max){
      break;
    }
    i++;
  }

  *es = i*nex + j;
  return 0;
}


int cmpfunc (const void * a, const void * b){

  return ( *(int*)a - *(int*)b );

}


int cmpfunc_for_list (void * a, void * b){

  return ( *(int*)a - *(int*)b );

}

/*

   SPUTNIK mesh treatment functions

   Author> Guido Giuntoli
   Date>   08-08-2017

 */

#include "sputnik.h"
#include "parmetis.h"

int part_mesh_PARMETIS(MPI_Comm *comm, FILE *time_fl, char *myname, double *centroid, int algorithm)
{

  /*
     Performes the mesh partition mesh saved on the mesh structures.

     a) First it builds the dual graph (nodes are elements)  of the 
     original (nodes are nodes)

     b) Do the partition 

     c) distribute the graph to processes

     Note>
     -> int *elmdist, int *eptr, int *eind, int *part are globals
   */

  int        rank, nproc, i,j, ierr;
  idx_t    * elmwgt;           // (inp) Element weights
  idx_t      wgtflag;          // (inp) Element weight flag (0 desactivated)
  idx_t      numflag;          // (inp) Numeration ( 0 in C, 1 in Fortran)
  idx_t      ncon;             // (inp) number of constrains of the graph ?
  idx_t      ncommonnodes;     // (inp) degree of connectivities among vertices in dual graph
  idx_t      nparts;           // (inp) number of partitions
  real_t   * tpwgts;           // (inp) array of size "ncon" x "npart" fraction of vertex for each subdomain
  real_t   * ubvec;            // (inp) array of size "ncon"
  idx_t      options[3];       // (inp) option parameters
  idx_t      edgecut;          // (out) number of edges cut of the partition

  MPI_Comm_size(*comm, &nproc);
  MPI_Comm_rank(*comm, &rank);

  nelm = elmdist[rank+1] - elmdist[rank];

  //**************************************************
  //
  // Set up some options
  //    
  elmwgt  = NULL; // no weights per elements
  wgtflag = 0;    // no weights per elements
  numflag = 0;    // C numeration

  nparts = nproc; // number of partitions 

  ncon = 1;
  tpwgts = (real_t*)malloc(ncon * nparts * sizeof(real_t));
  for(i=0; i < ncon * nparts ;i++){
    // uniform distribution of vertex in all processes
    tpwgts[i] = 1.0 / nparts;
  }

  ncommonnodes = 3;

  options[0] = 0; // options (1,2) : 0 default, 1 considered
  options[1] = 0; // level of information returned
  options[2] = 0; // random seed

  ubvec = (real_t*)malloc(ncon * sizeof(real_t));
  for(i=0;i<ncon;i++){
    ubvec[i] = 1.05;
  }

  //**************************************************

  if(algorithm == PARMETIS_GEOMKWAY){

  }
  else if(algorithm == PARMETIS_GEOM){

  }
  else if(algorithm == PARMETIS_KWAY){

  }
  else if(algorithm == PARMETIS_MESHKWAY){

    // Performe the partition with no weights
    ParMETIS_V3_PartMeshKway (
	elmdist, eptr, eind, elmwgt, &wgtflag, &numflag,
	&ncon, &ncommonnodes, &nparts, tpwgts, ubvec,
	options, &edgecut, part, comm );


    /* 
       Graph distribution

       First we create an array "npe" that follows
       the same function as eptr but is a global 
       reference 

       eptr = [ 0 3 5 8 9 ]
       npe  = [ 3 2 3 1 ]    (npe[i] = eptr[i+1] - eptr[i])

       Then vectors are switched acording to "part"
       we create npe_swi, eind_swi, npe_swi_size

       We do MPI_Alltoall of : "npe_swi_size"  ->  "npe_size_new"
       "eind_swi_size" ->  "eind_size_new"

       we free and reallocate memory for : "npe" using "npe_size" 
       "eind" using "eind_size" 

     */

    int *eind_swi, *eind_swi_size, *eind_size_new;
    int *npe_swi, *npe_swi_size, *npe_size_new;         
    int *PhysicalID_swi;
    int *npe;

    npe = malloc(nelm*sizeof(int));
    for(i=0;i<nelm;i++){
      npe[i] = eptr[i+1] - eptr[i];
    }

    eind_swi       = malloc(eptr[nelm]*sizeof(int)); 
    npe_swi        = malloc(nelm*sizeof(int)); 
    PhysicalID_swi = malloc(nelm*sizeof(int)); 
    eind_swi_size  = malloc(nproc*sizeof(int)); 
    npe_swi_size   = malloc(nproc*sizeof(int)); 
    eind_size_new  = malloc(nproc*sizeof(int)); 
    npe_size_new   = malloc(nproc*sizeof(int)); 

    // swap "npe" and "eind"
    ierr = swap_vectors_SCR( part, nproc, nelm, 
	npe, eptr, eind, PhysicalID,
	npe_swi, eind_swi, PhysicalID_swi,
	npe_swi_size, eind_swi_size );CHKERRQ(ierr);


    ierr = MPI_Alltoall(npe_swi_size, 1, MPI_INT, npe_size_new, 1, MPI_INT, *comm);CHKERRQ(ierr);
    ierr = MPI_Alltoall(eind_swi_size, 1, MPI_INT, eind_size_new, 1, MPI_INT, *comm);CHKERRQ(ierr);

    // free & reallocate memory for "npe" & "eind"
    int npe_size_new_tot = 0, eind_size_new_tot = 0;

    for(i=0;i<nproc;i++){
      npe_size_new_tot += npe_size_new[i];
      eind_size_new_tot += eind_size_new[i];
    }
    nelm = npe_size_new_tot;

    free(npe);
    free(eptr);
    free(eind);
    free(PhysicalID);

    npe  = malloc(nelm*sizeof(int));
    eptr = malloc((nelm+1)*sizeof(int));
    eind = malloc(eind_size_new_tot * sizeof(int));
    PhysicalID = malloc(nelm * sizeof(int));

    /* 
       performe the MPI_Alltoall operation for calculating "npe" & "eind"

       for "npe"
       sdispls = npe_swi_size
       rdispls = npe_size_new

       for "eind"
       sdispls = eind_swi_size
       rdispls = eind_size_new
     */

    int *sdispls, *rdispls;

    sdispls = malloc(nproc*sizeof(int)); 
    rdispls = malloc(nproc*sizeof(int)); 

    for(i=0;i<nproc;i++){
      sdispls[i] = 0;
      for(j=0;j<i;j++){
	sdispls[i] += npe_swi_size[j];
      }
    }
    for(i=0;i<nproc;i++){
      rdispls[i] = 0;
      for(j=0;j<i;j++){
	rdispls[i] += npe_size_new[j];
      }
    }

    ierr = MPI_Alltoallv(npe_swi, npe_swi_size, sdispls, MPI_INT, 
	npe, npe_size_new, rdispls, MPI_INT, *comm);CHKERRQ(ierr);

    ierr = MPI_Alltoallv(PhysicalID_swi, npe_swi_size, sdispls, MPI_INT, 
	PhysicalID, npe_size_new, rdispls, MPI_INT, *comm);CHKERRQ(ierr);

    // rebuild "eptr"
    eptr[0] = 0;
    for(i=0;i<nelm;i++){
      eptr[i+1] = eptr[i] + npe[i];
    }

    for(i=0;i<nproc;i++){
      sdispls[i] = 0;
      for(j=0;j<i;j++){
	sdispls[i] += eind_swi_size[j];
      }
    }
    for(i=0;i<nproc;i++){
      rdispls[i] = 0;
      for(j=0;j<i;j++){
	rdispls[i] += eind_size_new[j];
      }
    }

    ierr = MPI_Alltoallv(eind_swi, eind_swi_size, sdispls, MPI_INT, 
	eind, eind_size_new, rdispls, MPI_INT, *comm);

    if(flag_print & (1<<PRINT_ALL)){
      printf("%-6s r%2d %-20s : %8d\n", myname, rank, "new # of elements", npe_size_new_tot);

      printf("%-6s r%2d %-20s :", myname, rank, "npe_swi_size");
      for(i=0;i<nproc;i++){
	printf("%8d ",npe_swi_size[i]);
      }
      printf("\n");

      printf("%-6s r%2d %-20s :", myname, rank, "eind_swi_size");
      for(i=0;i<nproc;i++){
	printf("%8d ",eind_swi_size[i]);
      }
      printf("\n");

      printf("%-6s r%2d %-20s : %8d\n", myname, rank, "eind_size_new_tot", eind_size_new_tot);

      printf("%-6s r%2d %-20s : ", myname, rank, "eind");
      for(i=0;i<eptr[nelm];i++){
	printf("%3d ",eind[i]);
      }
      printf("\n");

      printf("%-6s r%2d %-20s :", myname, rank, "npe_size_new");
      for(i=0;i<nproc;i++){
	printf("%8d ",npe_size_new[i]);
      }
      printf("\n");

      printf("%-6s r%2d %-20s :", myname, rank, "eind_size_new");
      for(i=0;i<nproc;i++){
	printf("%8d ",eind_size_new[i]);
      }
      printf("\n");
    }
    free(eind_swi);
    free(npe_swi);
    free(npe_swi_size);
    free(npe_size_new);
    free(eind_swi_size);
    free(eind_size_new);
    free(sdispls);
    free(rdispls);
    free(PhysicalID_swi);
  }
  else{
    return 1;
  }
  free(ubvec);
  free(tpwgts);

  /*
     We delete repeated nodes and save the <nallnods> values on <allnods> in order
  */
  allnods = NULL;
  clean_vector_qsort(eptr[nelm], eind, &allnods, &nallnods);

  return 0;
}
/****************************************************************************************************/
int swap_vector( int *swap, int n, int *vector, int *new_vector, int *cuts )
{
  /*
     swaps a vector
     example>

     swap       = [ 0 1 0 0 1 2 2 ]
     vector     = [ 0 1 2 3 4 5 6 ]
     n = 7

     new_vector = [ 0 2 3 1 4 5 6 ] 
     cut        = [ 3 2 2 ]

     Notes>

     -> if new_vector = NULL the result is saved on vector
     -> swap should have values in [0,n)
   */

  int *aux_vector;
  int i,p,aux,j;

  if(n==0){
    return 0;
  }
  else if(vector == NULL || cuts == NULL){
    return 1;
  }

  if(new_vector == NULL){
    aux_vector = vector;
  }
  else{
    aux_vector = new_vector;
  }

  j = 0;
  for(p=0;p<n;p++){
    cuts[p] = 0;
    for(i=0;i<n;i++){
      if(swap[i] == p){
	aux=vector[i];
	aux_vector[i] = vector[j];
	aux_vector[j] = aux;
	j ++;
	cuts[p] ++;
      }
    }
  }

  return 0;
}
/****************************************************************************************************/
int swap_vectors_SCR( int *swap, int nproc, int n,  int *npe, 
    int *eptr, int *eind, int *PhysicalID,
    int *npe_swi, int *eind_swi, int *PhysicalID_swi,
    int *npe_size, int *eind_size )
{
  /*
     swaps a vectors in SCR format 
    
     example>
    
     swap        = [ 0 2 1 0 ] (swap will be generally the "part" array)
     npe         = [ 3 2 3 1 ]             
     PhysicalID  = [ 0 0 1 2 ]             
     eind        = [ 3 2 0 | 1 2 | 1 0 1 |3 ]
       
     swap operation with swap_vectors_CSR 
      
     npe_swi     = [ 3 1 3 2 ]
     PhysicalID  = [ 0 2 1 0 ]
     eind_swi    = [ 3 2 0 | 3 | 1 0 1 | 1 2 ]
    
     Notes>
    
     -> <n> is the length of <npe>
     -> <eptr> is used to identify quickly the <eind> values to be swapped
     -> results are saved on <eind_swi> and <npe_swi> (memory is duplicated)
     -> swap should have values in [0,nproc)
   */

  int e, p, i, j, lp, pi, c;

  if(n==0) return 0;

  if(!npe || !eind || !PhysicalID ||
      !eind_swi || !npe_swi || !PhysicalID_swi || 
      !npe_size || !eind_size){
    return 1;
  }

  j = pi = lp = 0;
  for(p=0;p<nproc;p++){

    npe_size[p] = 0;
    for(e=0;e<n;e++){

      if(swap[e] == p){

	// swap npe
	npe_swi[j] = npe[e];
	PhysicalID_swi[j] = PhysicalID[e];
	j ++;

	// swap eind
	// CSR_give_pointer( e, npe, eind, &pi);
	pi = eptr[e];

	for(i=0;i<npe[e];i++){
	  eind_swi[lp] = eind[ pi + i ];
	  lp ++;
	}
	npe_size[p] ++;
      }
    }
  }

  c = 0;
  for(i=0;i<nproc;i++){
    eind_size[i] = 0;
    for(j=0;j<npe_size[i];j++){
      eind_size[i] += npe_swi[c];
      c++;
    }
  }

  return 0;
}
/****************************************************************************************************/
int CSR_give_pointer( int e, int *npe, int *eind, int *p)
{
  /*
     Returns the position "p" inside "eind" of element "e"
    
   */
  int m;
  *p = 0;
  for(m=0;m<e;m++){
    *p += npe[m];
  }
  return 0;
}
/****************************************************************************************************/
int read_boundary(MPI_Comm PROBLEM_COMM, char *mesh_n, int mesh_f)
{
  /*
     Read boundary nodes and completes the structure <boundary>
   */
  if(mesh_f == FORMAT_GMSH){
    return read_boundary_GMSH(PROBLEM_COMM, mesh_n);
  }
  else if(mesh_f == FORMAT_ALYA){
    return read_boundary_ALYA(PROBLEM_COMM, mesh_n);
  }
  else{
    return 1;
  }
}
/****************************************************************************************************/
int read_boundary_GMSH(MPI_Comm PROBLEM_COMM, char *mesh_n)
{

  /* 
     Completes the <boundary.Nods> list with the list of
     nodes on the boundary that has <boundary.GmshID>

     Input> 
     char      mesh_n           > file name with path
     list_t    boundary.GmshID  > list of boundaries to search in mesh file

     Output> 
     list_t    boundary.Nods  > list of boundaries to search in mesh file

     1) We determine which elements that follow $Elements are surface elements
     we look if one of it nodes belongs to <mynods> if the answer is YES 
     we add in sort exclusive way to an auxiliary list called 
     <boundary_list_aux> that stores <boundary_aux_t> structures. The node is 
     added to the correspondent GmshID of that surface element. If NO we 
     continue with the others.

     2) For each element in the list we allocate memory (<NNods>) and copy on
     the array <Nods> the values saved on the <boundary.Nods> for the 
     correspondent <GmshID>

     Note> No MPI communicator is need here
   */

  FILE   *fm;

  int    total;
  int    i, d, n; 
  int    ln;                // line counter
  int    ntag;              // ntag to read gmsh element conectivities
  int    GmshIDToSearch; 
  int    NodeToSearch; 
  int    *pNodeFound; 
  int    NPE;

  char   buf[NBUF];   
  char   *data;

  node_list_t  *pBound;

  fm = fopen(mesh_n,"r"); if(!fm)SETERRQ1(PROBLEM_COMM,1,"file %s not found",mesh_n);
  /*
     Ahora hay que completar la lista <Nods>
   */
  while(fgets(buf,NBUF,fm)!=NULL){
    data=strtok(buf," \n");
    /*
       leemos hasta encontrar $Elements
     */
    if(!strcmp(data,"$Elements")){
      /*
	 leemos el numero total pero no lo usamos 
	 (incluye elementos de superficie y de volumen)
       */
      fgets(buf,NBUF,fm);
      data  = strtok(buf," \n");
      total = atoi(data);
      /*
	 leemos hasta $EndElements o encontramos un elemento de
	 de volumen. En general todos los de superficie estan
	 antes que los de volumen en Gmsh.
       */
      i=0;
      while(i<total)
      {
	fgets(buf,NBUF,fm); ln ++;
	data=strtok(buf," \n");
	data=strtok(NULL," \n");

	if(gmsh_is_surf_elm(atoi(data))){

	  NPE = gmsh_npe(atoi(data));
	  data=strtok(NULL," \n");
	  ntag = atoi(data);
	  // read the GmshID (or the same as PhysicalID for volumes)
	  data = strtok(NULL," \n");
	  GmshIDToSearch = atoi(data);

	  // search in <boundary_list> the element with the same <GmshIDToSearch>
	  pBound = boundary_list.head;
	  while(pBound){
	    if( ((boundary_t *)pBound->data)->GmshID == GmshIDToSearch ) break;
	    pBound = pBound->next;
	  }

	  if(pBound){
	    // salteamos los tags y nos vamos derecho para los nodos
	    d = 1;
	    while(d<ntag){
	      data = strtok(NULL," \n");
	      d++;
	    }
	    n=0;
	    while(n<NPE){
	      data = strtok(NULL," \n");
	      NodeToSearch = atoi(data);
	      pNodeFound = bsearch( &NodeToSearch, mynods, nmynods, sizeof(int), cmpfunc);
	      if(pNodeFound){
		list_insert_se( &(((boundary_t*)pBound->data)->Nods), pNodeFound);
	      }
	      n++;
	    }
	  }
	  else{
            /* Warning a physical entity specified on Gmsh file will not have boundary condition */
	  }

	}
	else{
	  /* 
	     is a volume element so I expect not to 
	     see surface elements far beyond this file
	   */
	  break;
	}
	i++;
      } // i < total de elementos especificados en Gmsh file
      break;
    } // inside $Elements - $EndElements
    ln ++;
  }
  fclose(fm);

  //
  /**************************************************/
  return 0;   
}
/****************************************************************************************************/
int read_boundary_ALYA(MPI_Comm PROBLEM_COMM, char *mesh_n)
{

  FILE   *fm_sizes, *fm_bound, *fm_onbound;

  int    i=0, bound_tot=-1, id_to_search=-1, node=-1, *pnod=NULL; 

  char   buf[NBUF], file_name_sizes[NBUF], file_name_bound[NBUF], file_name_onbound[NBUF], *data;

  node_list_t  *pBound;

  /**************************************************/
  /*
     first open <mesh_n>_SIZES.alya and read nelm_tot
  */
  strcpy(file_name_sizes,mesh_n);
  strcat(file_name_sizes,"_SIZES.alya");
  fm_sizes = fopen(file_name_sizes,"r"); if(!fm_sizes)SETERRQ1(PROBLEM_COMM,1,"file %s not found",file_name_sizes);
  while(fgets(buf,NBUF,fm_sizes)!=NULL){
    data=strtok(buf," \n");
    if(!strcmp(data,"BOUNDARIES=")){
      data=strtok(NULL," \n");
      bound_tot = atoi(data);
      break;
    }
  }
  fclose(fm_sizes);
  //
  /**************************************************/

  /**************************************************/
  /*
     open <mesh_n>_BOUNDARY.alya & <mesh_n>_ON_BOUNDARY.alya
     at the same time to know for each element the id
  */
  strcpy(file_name_bound,mesh_n);
  strcat(file_name_bound,"_BOUNDARIES.alya");
  fm_bound = fopen(file_name_bound,"r");if(!fm_bound)SETERRQ1(PROBLEM_COMM,1,"file %s not found",file_name_bound);

  strcpy(file_name_onbound,mesh_n);
  strcat(file_name_onbound,"_ON_BOUNDARIES.alya");
  fm_onbound = fopen(file_name_onbound,"r");if(!fm_onbound)SETERRQ1(PROBLEM_COMM,1,"file %s not found",file_name_onbound);

  if(!fgets(buf,NBUF,fm_bound))SETERRQ1(PROBLEM_COMM,1,"format error on %s",file_name_bound);
  if(!fgets(buf,NBUF,fm_onbound))SETERRQ1(PROBLEM_COMM,1,"format error on %s",file_name_onbound);

  while(i<bound_tot){

    /*
       read the id to search and search the element that corresponds to it 
       in the boundary_list
    */
    if(!fgets(buf,NBUF,fm_onbound))SETERRQ1(PROBLEM_COMM,1,"format error on %s",file_name_onbound);
    data=strtok(buf," \n");
    data=strtok(NULL," \n");
    id_to_search = atoi(data);
    pBound = boundary_list.head;
    while(pBound){
      if( ((boundary_t *)pBound->data)->GmshID == id_to_search ) break;
      pBound = pBound->next;
    }

    /*
       read the nodes
    */
    if(!fgets(buf,NBUF,fm_bound))SETERRQ1(PROBLEM_COMM,1,"format error on %s",file_name_bound);

    if(pBound){
      data=strtok(buf," \n");
      data=strtok(NULL," \n");
      while(data){
	node = atoi(data);
	pnod = bsearch( &node, mynods, nmynods, sizeof(int), cmpfunc);
	if(pnod){
	  list_insert_se( &(((boundary_t*)pBound->data)->Nods), pnod);
	}
	data = strtok(NULL," \n");
      }
    }
    else{
      /* Warning a physical entity specified on Gmsh file will not have boundary condition */
    }

    i++;
  } 
  fclose(fm_bound);
  fclose(fm_onbound);
  //
  /**************************************************/
  return 0;   
}
/****************************************************************************************************/
int gmsh_npe(int code)
{
    switch(code){
	case 1:  
	    return 2; 
	case 2:    
	    return 3;
	case 3:    
	    return 4;
	case 4:    
	    return 4;
	case 5:    
	    return 8;
	case 6:   
	    return 6;
	case 15:   
	    return 1;
	default:    
	    return -1;
    }
}
/****************************************************************************************************/
int gmsh_is_surf_elm(int code)
{
  // returns 1 if the code corresponds to a surface element, 0 othewhise
  if(dim == 2){
    return (code == 1 || code == 15) ? 1 : 0;
  }
  else if(dim == 3){
    return (code == 1 || code == 2 || code == 3 || code == 15) ? 1 : 0;
  }
  return 1;
}
/****************************************************************************************************/
int gmsh_is_vol_elm(int code)
{
  // returns 1 if the code corresponds to a volume element, 0 othewhise
  if(dim == 2){
    return (code == 2 || code == 3) ? 1 : 0;
  }
  else if(dim == 3){
    return (code == 4 || code == 5 || code == 6) ? 1 : 0;
  }
  return 1;
}
/****************************************************************************************************/
int read_mesh_coord(MPI_Comm PROBLEM_COMM, char *mesh_n, int mesh_f)
{
  /*
     Reads the mesh coordinate according to the format specified
   */
  if(mesh_f == FORMAT_GMSH){
    return read_mesh_coord_GMSH(PROBLEM_COMM, mesh_n);
  }
  else if(mesh_f == FORMAT_ALYA){
    return read_mesh_coord_ALYA(PROBLEM_COMM, mesh_n);
  }
  else{
    return 1;
  }
}
/****************************************************************************************************/
int read_mesh_coord_GMSH(MPI_Comm PROBLEM_COMM, char *mesh_n)
{

  /* 
     Info>   Reads the coordinates of the mesh

     Input> 
     char   * mesh_n   > file name with path
     MPI_Comm comm     > the communicator of these processes

     Output>
     int  * coord      > nodes' coordinates

   */

  FILE                 *fm;

  int                  i, c, d; 
  int                  ln, offset;  // line counter and offset for moving faster in the file
  int                  rank, nproc;

  char                 buf[NBUF];   
  char                 *data;

  MPI_Comm_size(PROBLEM_COMM, &nproc);
  MPI_Comm_rank(PROBLEM_COMM, &rank);

  fm = fopen(mesh_n,"r"); if(!fm)SETERRQ1(PROBLEM_COMM,1,"file %s not found",mesh_n);

  coord = malloc( nallnods*3 * sizeof(double));

  /**************************************************/
  //  go to "$Nodes" and then read coordinates 
  //  of nodes in <mynods> position
  //
  offset   = 0;
  while(fgets(buf,NBUF,fm)!=NULL){
    offset += strlen(buf); 
    data=strtok(buf," \n");
    //
    // leemos hasta encontrar $Elements
    //
    if(strcmp(data,"$Nodes")==0){
      //
      // leemos de nodos pero no lo usamos 
      // solo para hacer verificaciones
      //
      fgets(buf,NBUF,fm);
      offset += strlen(buf); 
      data  = strtok(buf," \n");
      NTotalNod = atoi(data);

      //
      // leemos todos los nodos en <mynods>
      //
      i = c = 0;
      while( c < nmynods ){
	while( i< mynods[c] ){
	  fgets(buf,NBUF,fm); 
	  i++;
	}
	data=strtok(buf," \n");
	for( d=0;d<3;d++){
	  data=strtok(NULL," \n");
	  coord[c*3 + d] = atof(data);
	}
	c++;
      }
      if(c>NTotalNod){
	printf("read_mesh_coord_GMSH : more nodes (%d) in %s than calculated (%d)\n", NTotalNod, mesh_n, c);
	return 1;
      }
      break;
    }
    ln ++;
  }
  fseek( fm, offset, SEEK_SET);      // we go up to the first volumetric element
  //
  // leemos todos los nodos en <ghost>
  //
  i = c = 0;
  while( c < nghost ){
    while( i<ghost[c] ){
      fgets(buf,NBUF,fm); 
      i++;
    }
    data=strtok(buf," \n");
    for( d=0;d<3;d++){
      data=strtok(NULL," \n");
      coord[ (nmynods + c)*3 + d] = atof(data);
    }
    c++;
  }
  if(c>NTotalNod){
    printf("read_mesh_coord_GMSH : more nodes (%d) in %s than calculated (%d)\n", NTotalNod, mesh_n, c);
    return 1;
  }
  fclose(fm);
  return 0;
}
/****************************************************************************************************/
int read_mesh_coord_ALYA(MPI_Comm PROBLEM_COMM, char *mesh_n)
{

  /* 
     Info>   Reads the coordinates of the mesh

     Input> 
     char   * mesh_n   > file name with path
     MPI_Comm comm     > the communicator of these processes

     Output>
     int  * coord      > nodes' coordinates

   */

  FILE  *fm;

  int   i, c, d; 
  int   rank, nproc;

  char  buf[NBUF], file_name[NBUF], *data;

  MPI_Comm_size(PROBLEM_COMM, &nproc);
  MPI_Comm_rank(PROBLEM_COMM, &rank);

  /**************************************************/
  /*
     first open <mesh_n>_SIZES.alya and read nelm_tot
  */
  strcpy(file_name,mesh_n);
  strcat(file_name,"_SIZES.alya");
  fm = fopen(file_name,"r"); if(!fm)SETERRQ1(PROBLEM_COMM,1,"file %s not found",file_name);
  while(fgets(buf,NBUF,fm)!=NULL){
    data=strtok(buf," \n");
    if(!strcmp(data,"NODAL_POINTS=")){
      data=strtok(NULL," \n");
      NTotalNod = atoi(data);
      break;
    }
  }
  fclose(fm);
  //
  /**************************************************/

  /**************************************************/
  /*
     first open <mesh_n>_SIZES.alya and read nelm_tot
  */
  coord = malloc( nallnods*3 * sizeof(double));

  strcpy(file_name,mesh_n);
  strcat(file_name,"_COORDINATES.alya");
  fm = fopen(file_name,"r"); if(!fm)SETERRQ1(PROBLEM_COMM,1,"file %s not found",file_name);

  /*
     leemos todos los nodos en <mynods>
  */
  if(!fgets(buf,NBUF,fm))SETERRQ1(PROBLEM_COMM,1,"format error on %s",file_name);
  i = c = 0;
  while( c < nmynods ){
    while( i< mynods[c] ){
      if(!fgets(buf,NBUF,fm))SETERRQ1(PROBLEM_COMM,1,"format error on %s",file_name);
      i++;
    }
    data=strtok(buf," \n");
    for( d=0;d<3;d++){
      data=strtok(NULL," \n");
      coord[c*3 + d] = atof(data);
    }
    c++;
  }

  /*
     leemos todos los nodos en <ghost>
  */
  rewind(fm);
  if(!fgets(buf,NBUF,fm))SETERRQ1(PROBLEM_COMM,1,"format error on %s",file_name);
  i = c = 0;
  while( c < nghost ){
    while( i<ghost[c] ){
      if(!fgets(buf,NBUF,fm))SETERRQ1(PROBLEM_COMM,1,"format error on %s",file_name);
      i++;
    }
    data=strtok(buf," \n");
    for( d=0;d<3;d++){
      data=strtok(NULL," \n");
      coord[ (nmynods + c)*3 + d] = atof(data);
    }
    c++;
  }
  fclose(fm);
  return 0;
}
/****************************************************************************************************/
int read_mesh_elmv(MPI_Comm PROBLEM_COMM, char *myname, char *mesh_n, int mesh_f)
{
  /*
     Reads elements of the mesh according to the format specified
   */
  if(mesh_f == FORMAT_GMSH){
    return read_mesh_elmv_CSR_GMSH(PROBLEM_COMM, myname, mesh_n);
  }
  else if(mesh_f == FORMAT_ALYA){
    return read_mesh_elmv_CSR_ALYA(PROBLEM_COMM, myname, mesh_n);
  }
  else{
    return 1;
  }
}
/****************************************************************************************************/
int read_mesh_elmv_CSR_GMSH(MPI_Comm PROBLEM_COMM, char *myname, char *mesh_n)
{
  /* 
     Info>   Reads the elements with the nodes conectivities and saves on 
     "elmdist[]", "eptr[]" and "eind[]" in CSR format (same names
     that parmetis)

     Input> 
     char   * mesh_n   > file name with path
     MPI_Comm comm     > the communicator of these processes

     Output>
     int  * elmdist  > number of elements for each process             (MAH)
     int  * eptr     > array of indeces for "eind" (CSR format)        (MAH)
     int  * eind     > element conectivities with nodes (CSR format)   (MAH)


     1) first counts the total number of volumetric element on the mesh nelm_tot

     2) calculates nelm = nelm_tot/nproc (elements assigned to this process)
     calculates the vector elmdist in order to know how many elems will be for each process 

     3) read the mesh again, each process reads its own group of elements and see
     element types determines "npe" and fills "eptr[nelm+1]"
     finally alloc memory for "eind[eptr[nelm]]"

     4) reads the mesh again and fill "eind[]"

     Notes>

     a) rank and nproc are going to be respect to the communicator "comm"

     b) all processes do fopen and fread up to their corresponding position
     in the file

     c) int *elmdist, int *eptr, int *eind, int *part are globals

   */

  FILE               * fm;
  unsigned long int    offset;

  int                  nelm_tot=-1, npe=-1;
  int                  total;
  int                  resto;
  int                  i, d, n; 
  int                  len;               // strlen(buf) for adding to offset
  int                  ln;                // line counter
  int                  ntag;              // ntag to read gmsh element conectivities
  int                  rank;
  int                  nproc;

  char                 buf[NBUF];   
  char               * data;

  MPI_Comm_size(PROBLEM_COMM, &nproc);
  MPI_Comm_rank(PROBLEM_COMM, &rank);

  fm = fopen(mesh_n,"r"); if(!fm)SETERRQ1(PROBLEM_COMM,1,"file %s not found",mesh_n);

  /**************************************************/
  /*  count the total number of volumetric elements 
      nelm_tot
   */
  offset   = 0;
  nelm_tot = 0;
  while(fgets(buf,NBUF,fm)!=NULL){
    offset += strlen(buf); 
    data=strtok(buf," \n");
    /*
       leemos hasta encontrar $Elements
     */
    if(strcmp(data,"$Elements")==0){
      /*
         leemos el numero total pero no lo usamos 
         (incluye elementos de superficie y de volumen)
      */
      fgets(buf,NBUF,fm);
      offset += strlen(buf); 
      data  = strtok(buf," \n");
      total = atoi(data);

      /*
	 leemos hasta $EndElements
	 y contamos el numero total de los elementos volumen
       */
      for(i=0; i<total; i++){
	fgets(buf,NBUF,fm); 
	len = strlen(buf);
	data=strtok(buf," \n");
	data=strtok(NULL," \n");
	if(gmsh_is_vol_elm(atoi(data))){
	  nelm_tot ++;
	}
	else{
	  // is a surface element so be continue counting
	  ln ++;
	  offset += len; 
	}
      }
      break;
    }
    ln ++;
  }
  //
  /**************************************************/

  /**************************************************/
  /*  
      armamos el vector elmdist. 
      example> P0 tiene sus elementos entre 
      elmdist[0] a elemdist[1] (not included)
      los elementos que sobran a la division 
      nelm_tot/nproc los repartimos uno por 
      uno entre los primeros procesos
   */
  elmdist = (int*)calloc( nproc + 1 ,sizeof(int));
  resto = nelm_tot % nproc;
  elmdist[0] = 0;
  for(i=1; i < nproc + 1; i++){
    elmdist[i] = i * nelm_tot / nproc;
    if(resto>0){
      elmdist[i] += 1;
      resto --;
    }
  }

  if(flag_print & (1<<PRINT_ALL)){
    printf("%-6s r%2d %-20s : %8d\n", myname, rank, "nelm_tot", nelm_tot);
    printf("%-6s r%2d %-20s : ", myname, rank, "<elmdist>");
    for(i=0; i < nproc + 1; i++){
      printf("%8d ",elmdist[i]);
    }
    printf("\n");
  }

  /*
     ya podemos allocar el vector "eptr" su dimension es :
     número de elementos locales + 1 = nelm + 1
   */
  nelm = elmdist[rank+1] - elmdist[rank];
  eptr = malloc( (nelm + 1) * sizeof(int));
  PhysicalID = malloc( nelm * sizeof(int));
  part = malloc(nelm * sizeof(int));
  //
  /**************************************************/

  /**************************************************/
  /*   
       we read the file again (from offset) 
       to count number of nodes 
       per element and fill "eptr"
       with this vector we can alloc memory for "eind"
   */    
  fseek( fm, offset, SEEK_SET);      // we go up to the first volumetric element
  for(i=0; i<elmdist[rank]; i++){    // we go to the first element we have to store
    fgets(buf,NBUF,fm); 
    offset += strlen(buf); 
  }
  eptr[0] = 0;
  for(i=1; i<nelm+1; i++){
    fgets(buf,NBUF,fm); 
    data=strtok(buf," \n");
    data=strtok(NULL," \n");
    npe = -1;
    npe = gmsh_npe(atoi(data));
    if(npe<0) SETERRQ1(PROBLEM_COMM,1,"element type %d not recognized",atoi(data));
    eptr[i] = eptr[i-1] + npe; 
  }
  eind = malloc( eptr[nelm] * sizeof(int));
  //
  /**************************************************/


  /**************************************************/
  /*
     repetimos el proceso pero esta vez leemos los 
     nodos y completamos el vector "eind[eptr[nelm]]"
     empezamos a leer desde "offset"
   */
  fseek( fm, offset, SEEK_SET);         // we go up to the first volumetric element
  n = 0;
  for(i=0; i<nelm; i++){
    fgets(buf,NBUF,fm); 
    data=strtok(buf," \n");
    data=strtok(NULL," \n");
    npe = gmsh_npe(atoi(data));
    if(npe<0) SETERRQ1(PROBLEM_COMM,1,"element type %d not recognized",atoi(data));
    data=strtok(NULL," \n");
    ntag = atoi(data);
    // we read the PhysicalID
    data = strtok(NULL," \n");
    PhysicalID[i] = atoi(data);
    d = 1;
    while(d<ntag){
      data = strtok(NULL," \n");
      if(!data) return 1;
      d++;
    }
    d = 0;
    while(d<npe){
      data = strtok(NULL," \n");
      if(!data) return 1;
      eind[n+d] = atoi(data); 
      d++;
    }
    n += npe;
  }
  //
  /**************************************************/

  return 0;   
}
/****************************************************************************************************/
int read_mesh_elmv_CSR_ALYA(MPI_Comm PROBLEM_COMM, char *myname, char *mesh_n)
{

  FILE                 *fm;
  char                 file_name[NBUF];

  int                  nelm_tot=-1, npe=-1;
  int                  resto;
  int                  i, d, n; 
  int                  rank;
  int                  nproc;

  char                 buf[NBUF];   
  char               * data;

  MPI_Comm_size(PROBLEM_COMM, &nproc);
  MPI_Comm_rank(PROBLEM_COMM, &rank);

  /**************************************************/
  /*
     first open <mesh_n>_SIZES.alya and read nelm_tot
  */
  strcpy(file_name,mesh_n);
  strcat(file_name,"_SIZES.alya");
  fm = fopen(file_name,"r"); if(!fm)SETERRQ1(PROBLEM_COMM,1,"file %s not found",file_name);
  while(fgets(buf,NBUF,fm)!=NULL){
    data=strtok(buf," \n");
    if(!strcmp(data,"ELEMENTS=")){
      data=strtok(NULL," \n");
      nelm_tot = atoi(data);
      break;
    }
  }
  fclose(fm);

  elmdist = (int*)calloc( nproc + 1 ,sizeof(int));
  resto = nelm_tot % nproc;
  elmdist[0] = 0;
  for(i=1; i < nproc + 1; i++){
    elmdist[i] = i * nelm_tot / nproc;
    if(resto>0){
      elmdist[i] += 1;
      resto --;
    }
  }

  nelm = elmdist[rank+1] - elmdist[rank];
  eptr = malloc( (nelm + 1) * sizeof(int));
  PhysicalID = malloc( nelm * sizeof(int));
  part = malloc(nelm * sizeof(int));
  //
  /**************************************************/

  /**************************************************/
  /*
     we read the <mesh_n>_TYPES.alya 
     to count number of nodes 
     per element and fill "eptr"
     with this vector we can alloc memory for "eind"
   */    
  strcpy(file_name,mesh_n);
  strcat(file_name,"_TYPES.alya");
  fm = fopen(file_name,"r"); if(!fm)SETERRQ1(PROBLEM_COMM,1,"file %s not found",file_name);
  if(fgets(buf,NBUF,fm) == NULL) return 1;

  for(i=0; i<elmdist[rank]; i++){    // we go to the first element we have to store
    fgets(buf,NBUF,fm); 
  }

  eptr[0] = 0;
  for(i=1; i<nelm+1; i++){
    fgets(buf,NBUF,fm); 
    data=strtok(buf," \n");
    data=strtok(NULL," \n");
    npe = -1;
    switch(atoi(data)){
      case 37:
	npe = 8;
	break;
      default:
	SETERRQ1(PROBLEM_COMM,1,"element type %d not recognized",atoi(data));
    }
    if(npe<0)SETERRQ(PROBLEM_COMM,1,"npe could not be determine");
    eptr[i] = eptr[i-1] + npe; 
  }
  fclose(fm);
  eind = malloc( eptr[nelm] * sizeof(int));
  //
  /**************************************************/

  /**************************************************/
  /*
     Read nodes from <mesh_n>_ELEMENTS.alya 
  */
  strcpy(file_name,mesh_n);
  strcat(file_name,"_ELEMENTS.alya");
  fm = fopen(file_name,"r"); if(!fm)SETERRQ1(PROBLEM_COMM,1,"file %s not found",file_name);
  if(!fgets(buf,NBUF,fm)) SETERRQ1(PROBLEM_COMM,1,"error format at file %s trying to read ELEMENTS",file_name);

  for(i=0; i<elmdist[rank]; i++){    // we go to the first element we have to store
    fgets(buf,NBUF,fm); 
  }

  n = 0;
  for(i=0; i < nelm ; i++){
    fgets(buf,NBUF,fm); 
    data=strtok(buf," \n");
    data=strtok(NULL," \n");
    npe = eptr[i+1]-eptr[i];
    for(d=0;d<npe;d++){
      if(!data)SETERRQ1(PROBLEM_COMM,1,"error format at file %s reading nodes",file_name);
      eind[n+d] = atoi(data); 
      data=strtok(NULL," \n");
    }
    n += npe;
  }
  fclose(fm);
  //
  /**************************************************/

  /**************************************************/
  /*
     Read Physical IDs from <mesh_n>_MATERIALS.alya
  */
  strcpy(file_name,mesh_n);
  strcat(file_name,"_MATERIALS.alya");
  fm = fopen(file_name,"r"); if(!fm)SETERRQ1(PROBLEM_COMM,1,"file %s not found",file_name);

  if(!fgets(buf,NBUF,fm)) SETERRQ1(PROBLEM_COMM,1,"error format in %s trying to read ELEMENTS",file_name);

  for(i=0; i<elmdist[rank]; i++){    // we go to the first element we have to store
    if(!fgets(buf,NBUF,fm)) SETERRQ1(PROBLEM_COMM,1,"error format in %s trying to read ELEMENTS",file_name);
  }

  for(i=0; i < nelm ; i++){
    if(!fgets(buf,NBUF,fm)) SETERRQ1(PROBLEM_COMM,1,"error format at file %s trying to read ELEMENTS",file_name);
    data = strtok(buf," \n");
    data = strtok(NULL," \n");
    if(!data)SETERRQ1(PROBLEM_COMM,1,"error format in %s trying to read PhysicalID",file_name);
    PhysicalID[i] = atoi(data);
  }
  fclose(fm);
  //
  /**************************************************/

  return 0;   
}
/****************************************************************************************************/
int read_physical_entities(MPI_Comm PROBLEM_COMM, char *mesh_n, int mesh_f)
{
  /*
     Reads the physical entities of the mesh according to the format specified
     notice that "physical entities" are those defined only on gmsh but they
     appear on all kind of meshes in order to classify surface and volume elements
     with a name
   */
  if(mesh_f == FORMAT_GMSH){
    return read_physical_entities_GMSH(PROBLEM_COMM, mesh_n);
  }
  else if(mesh_f == FORMAT_ALYA){
    return read_physical_entities_ALYA(PROBLEM_COMM, mesh_n);
  }
  else{
    return 1;
  }
}
/****************************************************************************************************/
int read_physical_entities_GMSH(MPI_Comm PROBLEM_COMM, char *mesh_n)
{
  /* 
     Info>     Reads the physical entities from the GMSH file
     >         and save them on <physical_list>

     Input> 
     char    *mesh_n   > file name with path

     Output>
     list_t  physical_list > physical entities list
   */
  FILE *fm;
  int i, ntot, ln = 0;
  char buf[NBUF], *data;

  physical_t physical;

  fm = fopen(mesh_n,"r");
  if(!fm){
    PetscPrintf(PROBLEM_COMM,"format error at line %d on %s\n", ln, mesh_n);
    return 1;
  }

  /**************************************************/
  /*  
      go to "$PhysicalNames" and then read them
      filling physical_list
   */
  while(fgets(buf,NBUF,fm)!=NULL)
  {
    ln++;
    data=strtok(buf," \n");
    /*
       leemos hasta encontrar $PhysicalNames
     */
    if(strcmp(data,"$PhysicalNames")==0){
      /*
	 leemos la cantidad de physical entities
	 solo para hacer verificaciones
       */
      fgets(buf,NBUF,fm);
      data  = strtok(buf," \n");
      ntot = atoi(data);

      while(fgets(buf,NBUF,fm)!=NULL)
      {
	ln++;

	// dimension
	data=strtok(buf," \n");
	if(!data){
	  PetscPrintf(PROBLEM_COMM,"format error at line %d on %s\n", ln, mesh_n);
	  return 1;
	}
	if(!strcmp(data,"$EndPhysicalNames")) break;
	physical.dim = atoi(data);

	// GmshID
	data=strtok(NULL," \n");
	physical.GmshID = atoi(data);

	// name (le sacamos los \" de las puntas)
	data=strtok(NULL," \n");
	physical.name = malloc((strlen(data)-1)*sizeof(char)); 
	for(i=1;i<strlen(data)-1;i++){
	  physical.name[i-1] = data[i];
	}
	physical.name[i-1] = '\0';
	/* This flag is to check if we find it on input file */
	physical.FlagFound = 0;

	list_insertlast(&physical_list,&physical);

      }
      if(!strcmp(data,"$EndPhysicalNames")){
	if(physical_list.sizelist != ntot)
	  SETERRQ2(PROBLEM_COMM,1,"Error filling physical_list: %d specified on %s",ntot,mesh_n);
	return 0;
      }
    }
    ln ++;
  }
  return 0;
}
/****************************************************************************************************/
int read_physical_entities_ALYA(MPI_Comm PROBLEM_COMM, char *mesh_n)
{
  /* 
     Info>     Reads the physical entities from the GMSH file
     >         and save them on <physical_list>

     Input> 
     char    *mesh_n   > file name with path

     Output>
     list_t  physical_list > physical entities list
   */
  FILE                 *fm;

  int                  ln = 0;  // line counter and offset for moving faster in the file

  char                 buf[NBUF], file_name[NBUF], *data;

  physical_t physical;

  strcpy(file_name,mesh_n);
  strcat(file_name,".codes.alya");
  fm = fopen(file_name,"r"); if(!fm)SETERRQ1(PROBLEM_COMM,1,"file %s not found",file_name);

  while(fgets(buf,NBUF,fm)!=NULL)
  {
    ln++;
    data=strtok(buf," '\n");

    if(data){
      if(!strcmp(data,"$>")){

	// name (le sacamos los \' de las puntas)
	data=strtok(NULL," ,'\n");
	physical.name = strdup(data); 

	// GmshID
	data=strtok(NULL," ,:\n");
	data=strtok(NULL," ,\n");
	physical.GmshID = atoi(data);

	// dimension (do not specify)
	physical.dim = -1;

	/* This flag is to check if we find it on input file */
	physical.FlagFound = 0;

	list_insertlast(&physical_list,&physical);
      }
    }
  }
  fclose(fm);
  return 0;
}
/****************************************************************************************************/
int clean_vector_qsort(int n, int *input, int **output, int *n_notrep)
{
  /*
     Deletes the values that are repeated in input and 
     write the new vector on output

     "n" is the size of "input"
     "n_notrep" is the size of "output"

     n >= n_notrep

     Note> use quick sort algorithm (n log n)

   */

  int  i, c, swi, val_o, *aux = NULL;

  if(n==0) return 0;
  if(*output) return 1;

  // we copy eind inside aux
  aux = malloc(n*sizeof(int));
  for(i=0;i<n;i++){
    aux[i] = input[i];
  }

  qsort(aux, n, sizeof(int), cmpfunc);

  val_o = aux[0];
  c = 1;
  for(i=1;i<n;i++){
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
  for(i=1;i<n;i++){
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
/****************************************************************************************************/
int give_repvector_qsort(MPI_Comm * comm, char *myname, int n, int *input, int **output, int *nrep)
{
  /*
     Returns a vector "output" with all the repetitions on "input"
      
     "n" is the size of "input"
     "nrep" is the size of "output"
    
     n >= nrep
    
     Note: use quick sort algorithm (n log n)
   */

  int   i, c, swi, val_o;
  int   *aux;
  int   rank;

  MPI_Comm_rank(*comm, &rank);

  (*nrep) = 0;

  // we copy eind inside aux
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
  if(flag_print & (1<<PRINT_ALL)){
    printf("%-6s r%2d %-20s : %8d\n", myname, rank, "n", n);
    printf("%-6s r%2d %-20s : %8d\n", myname, rank, "nrep" , *nrep);
  }

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
/****************************************************************************************************/
int give_inter_sort(MPI_Comm *comm, char *myname, int *array1, int n1, int *array2, int n2, int **reps, int *nreps)
{
  /*
     fills the "reps" array with nodes that repeated in both "array1" & "array2"

     Input > 
     array1 = [ 1 2 3 4 5 6 7 ]  n1 = 7
     array2 = [ 5 6 7 8 9 ]      n2 = 5

     Output > 
     reps   = [ 5 6 7 ]          nreps = 3


     Note> both arrays should be sorted in the same other
   */

  int i, j, c;

  // first we determine number of repetitions (count only once ) <nreps>
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

  // now fill <*reps>
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
/****************************************************************************************************/
int calculate_ghosts(MPI_Comm * comm, char *myname)
{
  /*
     This function determines which nodes of <*allnods> are <*ghost>

     strategy 

     1) Allgather operation sending <nallnods>

     2) all processes sends to all (using Isend) the array <allnods> 

   */

  int   i, j, c, g, rank, nproc;
  int   ierr;

  int   *peer_sizes, mysize, *peer_nod_glo;    // here we save the values <nallnods> coming from all the processes
  int   **repeated, *nrep;

  MPI_Request  *request;

  MPI_Comm_rank(*comm, &rank);
  MPI_Comm_size(*comm, &nproc);

  mysize     = nallnods;
  nghost    = 0;
  peer_sizes = NULL;

  peer_sizes = malloc(nproc*sizeof(int));
  request    = malloc(nproc*sizeof(MPI_Request));
  repeated   = calloc(nproc,sizeof(int*));
  nrep       = calloc(nproc,sizeof(int));

  ierr = MPI_Allgather(&mysize, 1, MPI_INT, peer_sizes, 1, MPI_INT, *comm);

  for(i=0;i<nproc;i++){
    if(i!=rank){
      ierr = MPI_Isend(allnods, mysize, MPI_INT, i, 0, *comm, &request[i]);CHKERRQ(ierr);
    }
  }
  for(i=0;i<nproc;i++){
    if(i!=rank){
      peer_nod_glo = malloc(peer_sizes[i]*sizeof(int));
      ierr = MPI_Recv(peer_nod_glo, peer_sizes[i], MPI_INT, i, 0, *comm, &status);
      give_inter_sort(comm, myname, allnods, mysize, peer_nod_glo, peer_sizes[i], &repeated[i], &nrep[i]);
      free(peer_nod_glo);
    }
  }

  if(flag_print & (1<<PRINT_ALL)){
    if(rank==0){
      printf("%-6s r%2d %-20s :", myname, rank, "nrep");
      for(i=0;i<nproc;i++){
	printf("%8d ", nrep[i]);
      }
      printf("\n");
    }
  }

  // condensamos en 1 vector todo lo que hay en repeated
  int *rep_array = NULL, nreptot = 0, *rep_array_clean = NULL, nreptot_clean = 0;

  for(i=0;i<nproc;i++){
    if(i!=rank){
      nreptot += nrep[i];
    }
  }
  rep_array = malloc(nreptot * sizeof(int));
  c = 0;
  for(i=0;i<nproc;i++){
    if(i!=rank){
      for(j=0;j<nrep[i];j++){
	rep_array[c] = repeated[i][j];
	c ++;
      }
    }
  }

  ierr = clean_vector_qsort(nreptot, rep_array, &rep_array_clean, &nreptot_clean);CHKERRQ(ierr);

  if(flag_print & (1<<PRINT_ALL)){
    printf("%-6s r%2d %-20s : %8f\n", myname, rank, "nreptot [%]", (nreptot_clean*100.0)/nallnods ); 
  }

  free(rep_array);

  // calculamos la cantidad de puntos dentro de <allnods> que me pertenecen

  int ismine, r, remoterank;

  if(nreptot_clean!=0){
    nmynods = nghost = r = 0;
    for(i=0;i<nallnods;i++){
      if(r<nreptot_clean){
	if(allnods[i] == rep_array_clean[r]){
	  ismine = ownership_selec_rule( comm, repeated, nrep, allnods[i], &remoterank);
	  r++;
	  if(ismine){
	    nmynods ++;
	  }
	  else{
	    nghost ++;
	  }
	}
	else{
	  nmynods ++;
	}
      }
      else{
	nmynods ++;
      }
    }
  }
  else{
    nmynods = nallnods;
    nghost = 0;
  }

  
  if(flag_print & (1<<PRINT_ALL)){
    printf("%-6s r%2d %-20s : %8f   %-20s : %8f\n", myname, rank, "nghost/nallnods [%]", (nghost*100.0)/nallnods,	"nmynods/nallnods [%]", (nmynods*100.0)/nallnods); 
  }

  mynods = malloc(nmynods*sizeof(int));
  ghost = malloc(nghost*sizeof(int));

  c = r = g = 0;
  if(nreptot_clean!=0){
    for(i=0;i<nallnods;i++){
      // podria ser un nodo que le pertenece a otro proceso
      if(r<nreptot_clean){
	if(allnods[i] == rep_array_clean[r]){
	  ismine = ownership_selec_rule( comm, repeated, nrep, allnods[i], &remoterank);
	  r++;
	  if(ismine){
	    mynods[c] = allnods[i];
	    c ++;
	  }
	  else{
	    ghost[g] = allnods[i];
	    g ++;
	  }
	}
	else{
	  // no está en la lista de repetidos
	  mynods[c] = allnods[i];
	  c ++;
	}
      }else{
	mynods[c] = allnods[i];
	c ++;
      }
    }
  }
  else{
    // no hay repetidos entonces es nuestro
    for(i=0;i<nallnods;i++){
      mynods[i] = allnods[i];
    }
  }


  // free memory for <repeated>
  for(i=0;i<nproc;i++){
    if(i!=rank){
      free(repeated[i]);
    }
  }
  free(repeated);
  free(nrep);
  free(request);
  free(peer_sizes);

  // >>>>> PRINT
  if(flag_print & (1<<PRINT_ALL)){
    printf("%-6s r%2d %-20s : %8d   %-20s : %8d\n", myname, rank, "nallnods", nallnods, "nmynods", nmynods);
    printf("%-6s r%2d %-20s : ", myname, rank, "allnods");
    for(i=0;i<nallnods;i++){
      printf("%3d ",allnods[i]);
    }
    printf("\n");
    printf("%-6s r%2d %-20s : ", myname, rank, "reps");
    for(i=0;i<nreptot_clean;i++){
      printf("%3d ",rep_array_clean[i]);
    }
    printf("\n");
    printf("%-6s r%2d %-20s : ", myname, rank, "mynods");
    for(i=0;i<nmynods;i++){
      printf("%3d ",mynods[i]);
    }
    printf("\n");
    printf("%-6s r%2d %-20s : ", myname, rank, "ghost");
    for(i=0;i<nghost;i++){
      printf("%3d ",ghost[i]);
    }
    printf("\n");
  }
  // >>>>> PRINT

  return 0;
}
/****************************************************************************************************/
int reenumerate_PETSc(MPI_Comm PROBLEM_COMM)
{
  /* 
     This routine

     a) creates and fills array <loc2petsc> of size <nmynods> + <nghost>
     in each local position <n> is stored the global position in PETSc matrix 

   */

  int   rank, nproc;
  int   i, j, *p, ierr; 
  int   *rem_nods;// buffer to receive mynods from the other processes
  int   *rem_nnod;   // buffers' sizes with nmynodsOrig from every process

  MPI_Comm_rank(PROBLEM_COMM, &rank);
  MPI_Comm_size(PROBLEM_COMM, &nproc);

  rem_nnod = malloc( nproc * sizeof(int));
  StartIndexRank = malloc( nproc * sizeof(int));
  ierr = MPI_Allgather(&nmynods, 1, MPI_INT, rem_nnod, 1, MPI_INT, PROBLEM_COMM);CHKERRQ(ierr);

  StartIndexRank[0] = 0;
  i = 1;
  while(i<nproc){
    StartIndexRank[i] = StartIndexRank[i-1] + rem_nnod[i-1];
    i++;
  }

  //**************************************************
  //  reenumeramos <eind>
  for(i=0;i<eptr[nelm];i++){
    // is a local node
    p = bsearch(&eind[i], mynods, nmynods, sizeof(int), cmpfunc);
    if(p != NULL){
      eind[i] = p - mynods;
    }
    else{
      // is a ghost node
      p = bsearch(&eind[i], ghost, nghost, sizeof(int), cmpfunc);
      if(p != NULL){
	eind[i] = nmynods + p - ghost;
      }
      else{
	SETERRQ1(PROBLEM_COMM,1,"value %d not found on <mynods> neither <ghost>",eind[i]);
      }
    }
  }

  loc2petsc = malloc( (nmynods + nghost) * sizeof(int));

  //**************************************************
  // empezamos con los locales
  for(i=0;i<nmynods;i++){
    loc2petsc[i] = StartIndexRank[rank] + i;
  }

  /*
    And now ghosts nodes>
   
    each process sends <mynods> 
    and each process receives that vector
    and search if any ghost is inside.
    With that information completes <GhostRank>
    and then using that completes finally <loc2petsc>
   */

  MPI_Request  *request;

  int   *MyGhostGlobalIndex;

  request    = malloc(nproc*sizeof(MPI_Request));
  MyGhostGlobalIndex = malloc(nghost*sizeof(int));
  for(i=0;i<nghost;i++){
    MyGhostGlobalIndex[i] = -1;
  }

  for(i=0;i<nproc;i++){
    if(i!=rank){
      ierr = MPI_Isend(mynods, nmynods, MPI_INT, i, 0, PROBLEM_COMM, &request[i]);
    }
  }
  for(i=0;i<nproc;i++){
    // receive from all peer ranks "i"
    if(i!=rank){
      rem_nods = malloc(rem_nnod[i]*sizeof(int));
      ierr = MPI_Recv(rem_nods, rem_nnod[i], MPI_INT, i, 0, PROBLEM_COMM, &status);
      for(j=0;j<nghost;j++){
	// search this ghost node on <rem_nods>
	p = bsearch(&ghost[j], rem_nods, rem_nnod[i], sizeof(int), cmpfunc);
	if(p!=NULL){
	  MyGhostGlobalIndex[j] = StartIndexRank[i] + p - rem_nods;
	}
      }
      free(rem_nods);
    }
  }

  // check if all the ghost where found remotely
  for(i=0;i<nghost;i++){
    if(MyGhostGlobalIndex[i] == -1){
      SETERRQ1(PROBLEM_COMM,1,"<ghost> value %d not found remotely",ghost[i]);
    }
  }

  for(i=0;i<nghost;i++){
    loc2petsc[nmynods + i] =  MyGhostGlobalIndex[i];
  }

  free(rem_nnod);
  free(MyGhostGlobalIndex);
  free(request);
  return 0;
}
/****************************************************************************************************/
int search_position_linear(int *array, int size, int val, int *pos)
{
  /* 
     Returns> 
     a) the position <pos> of <val> inside <array> (size <size>)
     b) <pos> = -1 if <val> does not exist 
    
   */

  int   i=0;

  while(i<size){
    if(array[i] == val){
      break;
    }
    i++;
  }

  if(i==size){
    *pos = -1;
  }
  else{
    *pos = i;
  }

  return 0;
}
/****************************************************************************************************/
int search_position_logn(int *array, int size, int val, int *pos)
{
  /* 
     Returns> 
     a) the position <pos> of <val> inside <array> (size <size>)
     b) <pos> = -1 if <val> does not exist 

     Note> the array should be sorted
   */

  int  left = 0, right = size-1, middle;

  while(left <= right){

    middle = (right + left)/2; 
    if(array[middle] == val){
      *pos = middle;
      return 0;
    }
    if(array[middle] < val){
      left = middle + 1;
    }
    else{
      right = middle - 1;
    }
  }
  *pos = -1;
  return 0;
}
/****************************************************************************************************/
int ownership_selec_rule( MPI_Comm *comm, int **repeated, int *nrep, int node, int *remoterank )
{

  /*  
      Function for determine the ownership of a repeated 
      node on different processors. 

      Input 

      repeated > list of nodes that each process have in common with me
      nrep     > number of elements in each <repeated> element
      node     > node numeration in order to know if this process owns it

      Notes>
      -> all process should return the same if <node> is the same
      -> the selection criteria calculates rankp = node % nproc as root
      if the rankp in repeated contains <node> in <rankp> position 
      then this is the ownership of it. If <rankp> = <rank> at any 
      part of the search then this node is of this process.
   */

  int nproc, rank;

  MPI_Comm_rank(*comm, &rank);
  MPI_Comm_size(*comm, &nproc);

  int i, rankp;

  // damos un guess inicial de <rankp> luego iremos buscamos 
  // hacia los ranks crecientes
  rankp = node % nproc; 

  i = 0;
  while(i<nproc){
    //tenemos un guess nuevo de rankp
    if(rankp == rank){
      // si justo nos cayo entonces este <node> es nuestro
      *remoterank = rankp;
      return 1;
    } 
    else{
      if(is_in_vector(node, &repeated[rankp][0], nrep[rankp])){
	// lo encontramos pero está en otro rank
	*remoterank = rankp;
	return 0;
      }
      else{
	// buscamos siempre a la derecha
	rankp ++;
	if(rankp == nproc){
	  rankp = 0;
	}
      }
    }
    i ++;
  }

  return -1;	
}
/****************************************************************************************************/
int is_in_vector(int val, int *vector, int size)
{
  /*  
      val     > value to search  
      vector 
      size    > # of components of vector
   */
  int j = 0;
  while(j<size){
    if(vector[j] == val){
      break;
    }
    j++;
  }
  if(j == size){
    // llegamos al final => no está
    return 0;
  }
  else{
    return 1;
  }

  return -1;
}
/****************************************************************************************************/
int set_id_on_material_and_boundary(MPI_Comm PROBLEM_COMM)
{
  /* 
     For each material on <material_list> 
     Searchs for the <GmshID> in the 
     <physical_list>
   */
  node_list_t *pm, *pp;

  pm = material_list.head;
  while(pm){
    pp = physical_list.head;
    while(pp){
      if( !strcmp( ((physical_t*)pp->data)->name, ((material_t*)pm->data)->name ) ){
	((material_t*)pm->data)->GmshID = ((physical_t*)pp->data)->GmshID;
	break;
      }
      pp = pp->next;
    }

    if(pp!=NULL){
      ((physical_t*)pp->data)->FlagFound = 1;
    }
    pm = pm->next;
  }

  pm = boundary_list.head;
  while(pm){
    pp = physical_list.head;
    while(pp){
      if( !strcmp( ((physical_t*)pp->data)->name, ((boundary_t*)pm->data)->name ) ){
	((boundary_t*)pm->data)->GmshID = ((physical_t*)pp->data)->GmshID;
	break;
      }
      pp = pp->next;
    }
    if(!pp){ 
      SETERRQ1(PETSC_COMM_SELF,1,"Boundary %s not found in Gmsh File.",((boundary_t*)pm->data)->name);
    }
    ((physical_t*)pp->data)->FlagFound = 1;
    pm = pm->next;
  }

  /* Check Physical not found a print a warning */
  pp = physical_list.head;
  while(pp)
  {
    if( !((physical_t*)pp->data)->FlagFound ){PetscPrintf(PROBLEM_COMM,
	"\nWARNING:Physical %s not found on boundary_list.\n",((physical_t*)pp->data)->name);}
    pp = pp->next;
  }
  return 0;
}
/****************************************************************************************************/
int get_bbox_local_limits(double *coord, int n, double *x, double *y, double *z)
{
  /*
     calculates x = [xmin xmax] y = [ymin ymax] z = [zmin zmax]

     coord = x0 y0 z0 x1 y1 z1 ... xn yn zn

     n > number of points (size(coord) = 3*n)
   */

  if(n==0) return 0;

  int i=0;
  x[0] = coord[i*3+0]; x[1] = coord[i*3+0];
  y[0] = coord[i*3+1]; y[1] = coord[i*3+1];
  z[0] = coord[i*3+2]; z[1] = coord[i*3+2];
  for(i=1;i<n;i++){
    if( coord[i*3+0] < x[0] ) x[0] = coord[i*3+0];
    if( coord[i*3+0] > x[1] ) x[1] = coord[i*3+0];
    if( coord[i*3+1] < y[0] ) y[0] = coord[i*3+1];
    if( coord[i*3+1] > y[1] ) y[1] = coord[i*3+1];
    if( coord[i*3+2] < z[0] ) z[0] = coord[i*3+2];
    if( coord[i*3+2] > z[1] ) z[1] = coord[i*3+2];
  }

  return 0;
}
/****************************************************************************************************/
int get_bbox_limit_lengths(MPI_Comm PROBLEM_COMM, double *coord, int n, double *lx, double *ly, double *lz)
{
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
/****************************************************************************************************/
int cmpfunc (const void * a, const void * b)
{
  return ( *(int*)a - *(int*)b );
}
/****************************************************************************************************/
int cmpfunc_for_list (void * a, void * b)
{
  return ( *(int*)a - *(int*)b );
}

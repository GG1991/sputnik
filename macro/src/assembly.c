#include "macro.h"

#ifdef PETSC

int assembly_b(double *norm){

  double *wp;
  double *b_arr;
  Vec b_loc;
  int ierr;

  VecGhostGetLocalForm(b, &b_loc);
  VecGetArray(b_loc, &b_arr);

  ARRAY_SET_TO_ZERO(b_arr, mesh.nnods_local_ghost*dim)

  for(int e = 0 ; e < mesh.nelm_local ; e++){

    get_local_elem_index(e, loc_elem_index);
    int npe = mesh.eptr[e+1] - mesh.eptr[e];
    int ngp = npe;

    ARRAY_SET_TO_ZERO(res_elem, npe*dim)

    ierr = get_dsh(e, loc_elem_index, dsh, detj);
    ierr = get_bmat(e, dsh, bmat);
    ierr = get_wp(dim, npe, &wp);

    for(int gp = 0; gp < ngp ; gp++){

      if(detj[gp] < 0.0) flag_neg_detj = 1;
      detj[gp] = fabs(detj[gp]);

      get_strain(e, gp, loc_elem_index, dsh, bmat, strain_gp);
      get_stress(e, gp, strain_gp, stress_gp);

      for(int i = 0 ; i < npe*dim ; i++){
	for(int j = 0; j < nvoi ; j++)
	  res_elem[i] += bmat[j][i][gp]*stress_gp[j]*wp[gp]*detj[gp];
      }
    }

    for(int i = 0 ; i < npe*dim ; i++)
      b_arr[loc_elem_index[i]] += res_elem[i];

  }

  VecRestoreArray(b_loc, &b_arr);
  VecGhostRestoreLocalForm(b, &b_loc);

  VecGhostUpdateBegin(b, ADD_VALUES, SCATTER_REVERSE);
  VecGhostUpdateEnd(b, ADD_VALUES, SCATTER_REVERSE);
  VecGhostUpdateBegin(b, INSERT_VALUES, SCATTER_FORWARD);
  VecGhostUpdateEnd(b, INSERT_VALUES, SCATTER_FORWARD);

  if(flag_neg_detj == 1)
    myio_printf(MACRO_COMM, "MACRO: warning negative jacobian detected\n");

  VecGhostGetLocalForm(b, &b_loc);
  VecGetArray(b_loc, &b_arr);

  node_list_t *pn = boundary_list.head;
  while(pn){
    mesh_boundary_t *bou = (mesh_boundary_t *)pn->data;
    for(int i = 0 ; i < bou->ndirix ; i++)
      b_arr[bou->dir_loc_ixs[i]] = 0.0;
    pn = pn->next;
  }

  VecRestoreArray(b_loc, &b_arr);
  VecGhostRestoreLocalForm(b, &b_loc);
  VecGhostUpdateBegin(b, INSERT_VALUES, SCATTER_FORWARD);
  VecGhostUpdateEnd(b, INSERT_VALUES, SCATTER_FORWARD);
  VecScale(b, -1.0);

  if(flags.print_vectors == true){
    PetscViewer viewer;
    PetscViewerASCIIOpen(MACRO_COMM, "b.dat", &viewer); VecView(b ,viewer);
  }

  VecNorm(b, NORM_2, norm);

  return ierr;
}


int assembly_AM(void){

  MatZeroEntries(A);
  MatZeroEntries(M);

  int ierr;
  double rho_gp;
  double **sh;
  double *wp;

  for(int e = 0 ; e < mesh.nelm_local ; e++ ){

    int npe = mesh.eptr[e+1] - mesh.eptr[e];
    int ngp = npe;

    ARRAY_SET_TO_ZERO(k_elem, npe*dim*npe*dim);
    ARRAY_SET_TO_ZERO(m_elem, npe*dim*npe*dim);

    get_local_elem_index ( e, loc_elem_index );
    get_global_elem_index( e, glo_elem_index );

    get_sh( dim, npe, &sh );
    get_dsh( e, loc_elem_index, dsh, detj);
    get_bmat( e, dsh, bmat );
    get_wp( dim, npe, &wp );

    for(int gp = 0; gp < ngp ; gp++){

      detj[gp] = fabs(detj[gp]);

      ierr = get_strain(e , gp, loc_elem_index, dsh, bmat, strain_gp);
      ierr = get_c_tan(NULL, e, gp, strain_gp, c); if(ierr != 0) return 1;
      ierr = get_rho(NULL, e, &rho_gp); if(ierr != 0) return 1;

      for(int i = 0 ; i < npe*dim ; i++){
	for(int j = 0 ; j < npe*dim ; j++){
	  for(int k = 0; k < nvoi ; k++){
	    for(int h = 0; h < nvoi ; h++)
	      k_elem[i*npe*dim + j] += bmat[h][i][gp]*c[h*nvoi + k]*bmat[k][j][gp]*wp[gp]*detj[gp];
	  }
	}
      }

      for(int d = 0 ; d < dim ; d++){
	for(int i = 0 ; i < npe; i++){
	  for(int j = 0 ; j < npe; j++)
	    m_elem[(i*dim)*(npe*dim) + j*dim + (d*dim*npe + d)] += rho_gp*sh[i][gp]*sh[j][gp]*wp[gp]*detj[gp];
	}
      }

    }
    MatSetValues(A, npe*dim, glo_elem_index, npe*dim, glo_elem_index, k_elem, ADD_VALUES);
    MatSetValues(M, npe*dim, glo_elem_index, npe*dim, glo_elem_index, m_elem, ADD_VALUES);

  }

  MatAssemblyBegin(A, MAT_FINAL_ASSEMBLY);
  MatAssemblyEnd(A, MAT_FINAL_ASSEMBLY);
  MatAssemblyBegin(M, MAT_FINAL_ASSEMBLY);
  MatAssemblyEnd(M, MAT_FINAL_ASSEMBLY);

  node_list_t *pn = boundary_list.head;
  while(pn != NULL){
    mesh_boundary_t * bou = (mesh_boundary_t * )pn->data;
    MatZeroRowsColumns(M, bou->ndirix, bou->dir_glo_ixs, 1.0, NULL, NULL);
    MatZeroRowsColumns(A, bou->ndirix, bou->dir_glo_ixs, 1.0, NULL, NULL);
    pn = pn->next;
  }
  MatAssemblyBegin(M, MAT_FINAL_ASSEMBLY);
  MatAssemblyEnd(M, MAT_FINAL_ASSEMBLY);
  MatAssemblyBegin(A, MAT_FINAL_ASSEMBLY);
  MatAssemblyEnd(A, MAT_FINAL_ASSEMBLY);

  if(flags.print_vectors == true){
    PetscViewer viewer;
    PetscViewerASCIIOpen(MACRO_COMM, "M.dat", &viewer); MatView(M, viewer);
    PetscViewerASCIIOpen(MACRO_COMM, "A.dat", &viewer); MatView(A, viewer);
  }

  return ierr;
}


int assembly_A(void){

  MatZeroEntries(A);

  int ierr;
  double *wp;

  for(int e = 0 ; e < mesh.nelm_local ; e++ ){

    int npe = mesh.eptr[e+1] - mesh.eptr[e];
    int ngp = npe;

    ARRAY_SET_TO_ZERO(k_elem, npe*dim*npe*dim);

    get_local_elem_index (e, loc_elem_index);
    get_global_elem_index(e, glo_elem_index);

    get_dsh(e, loc_elem_index, dsh, detj);
    get_bmat(e, dsh, bmat);
    get_wp(dim, npe, &wp);

    for(int gp = 0; gp < ngp ; gp++){

      detj[gp] = fabs(detj[gp]);

      get_strain(e, gp, loc_elem_index, dsh, bmat, strain_gp);

      ierr = get_c_tan(NULL , e , gp , strain_gp , c); if(ierr != 0) return ierr;

      for(int i = 0 ; i < npe*dim ; i++){
	for(int j = 0 ; j < npe*dim ; j++){
	  for(int k = 0; k < nvoi ; k++){
	    for(int h = 0; h < nvoi ; h++)
	      k_elem[ i*npe*dim + j] += bmat[h][i][gp]*c[h*nvoi + k]*bmat[k][j][gp]*wp[gp]*detj[gp];
	  }
	}
      }

    }
    MatSetValues(A, npe*dim, glo_elem_index, npe*dim, glo_elem_index, k_elem, ADD_VALUES);

  }

  MatAssemblyBegin(A, MAT_FINAL_ASSEMBLY);
  MatAssemblyEnd(A, MAT_FINAL_ASSEMBLY);

  node_list_t *pn = boundary_list.head;
  while(pn != NULL){
    mesh_boundary_t *bou = (mesh_boundary_t *)pn->data;
    MatZeroRowsColumns(A, bou->ndirix, bou->dir_glo_ixs, 1.0, NULL, NULL);
    pn = pn->next;
  }
  MatAssemblyBegin(A, MAT_FINAL_ASSEMBLY);
  MatAssemblyEnd(A, MAT_FINAL_ASSEMBLY);

  if(flags.print_vectors == true){
    PetscViewer viewer;
    PetscViewerASCIIOpen(MACRO_COMM, "A.dat", &viewer); MatView(A ,viewer);
  }

  return ierr;
}

#endif

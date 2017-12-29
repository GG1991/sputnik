#include "micro.h"


int mic_homogenize_taylor(double *strain_mac, double *strain_ave, double *stress_ave)
{
  double *c_i = malloc(nvoi*nvoi * sizeof(double));
  double *c_m = malloc(nvoi*nvoi * sizeof(double));
  double *c = malloc(nvoi*nvoi * sizeof(double));
  double vol_i = 0.0, vol_m = 0.0;

  int ne_i = 0, ne_m = 0;
  for (int e = 0; e < mesh_struct.nelm; e++) {
    if (elem_type[e] == ID_FIBER) {
      vol_i += mesh_struct.vol_elm;
      ne_i++;
    } else {
      vol_m += mesh_struct.vol_elm;
      ne_m++;
    }
  }

  vi = vol_i / mesh_struct.vol;
  vm = vol_m / mesh_struct.vol;
  PRINTF2("vi = %lf \n", vi);
  PRINTF2("vm = %lf \n", vm);

  get_c_tan("FIBER" , -1, -1, NULL, c_i);
  get_c_tan("MATRIX", -1, -1, NULL, c_m);

  if (params.homog_method == HOMOG_METHOD_TAYLOR_PARALLEL) {

    for (int i = 0; i < nvoi; i++) {
      for (int j = 0; j < nvoi; j++)
	c[i*nvoi + j] = vi * c_i[i*nvoi + j] + vm * c_m[i*nvoi + j];
    }

  } else if (params.homog_method == HOMOG_METHOD_TAYLOR_SERIAL) {

    double *c_mi = malloc(nvoi*nvoi*sizeof(double));
    double *c_ii = malloc(nvoi*nvoi*sizeof(double));
    gsl_matrix_view  gsl_c_m , gsl_c_i ;
    gsl_matrix_view  gsl_c_mi, gsl_c_ii;

    gsl_permutation *p = gsl_permutation_alloc(nvoi);

    gsl_c_i = gsl_matrix_view_array(c_i , nvoi, nvoi);
    gsl_c_m = gsl_matrix_view_array(c_m , nvoi, nvoi);
    gsl_c_ii = gsl_matrix_view_array(c_ii, nvoi, nvoi);
    gsl_c_mi = gsl_matrix_view_array(c_mi, nvoi, nvoi);

    int s;
    gsl_linalg_LU_decomp(&gsl_c_m.matrix, p, &s);
    gsl_linalg_LU_invert(&gsl_c_m.matrix, p, &gsl_c_mi.matrix);
    gsl_linalg_LU_decomp(&gsl_c_i.matrix, p, &s);
    gsl_linalg_LU_invert(&gsl_c_i.matrix, p, &gsl_c_ii.matrix);

    for (int i = 0; i < nvoi*nvoi; i++)
      c_i[i] = vi*c_ii[i] + vm*c_mi[i];

    gsl_linalg_LU_decomp(&gsl_c_i.matrix, p, &s);
    gsl_linalg_LU_invert(&gsl_c_i.matrix, p, &gsl_c_mi.matrix);

    for (int i = 0; i < nvoi; i++) {
      for (int j = 0; j < nvoi; j++)
	c[i*nvoi + j] = gsl_matrix_get( &gsl_c_mi.matrix, i, j );
    }

    gsl_permutation_free(p);
    free(c_mi);
    free(c_ii);
  }

  for (int i = 0; i < nvoi; i++) {
    strain_ave[i] = strain_mac[i];
    stress_ave[i] = 0.0;
    for (int j = 0; j < nvoi; j++)
      stress_ave[i] += c[i*nvoi + j] * strain_mac[j];
  }

  return 0;
}


int mic_homog_us(double *strain_mac, double *strain_ave, double *stress_ave)
{
  VecZeroEntries(x);
  VecGhostUpdateBegin(x, INSERT_VALUES, SCATTER_FORWARD);
  VecGhostUpdateEnd(x, INSERT_VALUES, SCATTER_FORWARD);

  Vec x_loc;
  double *x_arr;
  VecGhostGetLocalForm(x, &x_loc);
  VecGetArray(x_loc, &x_arr);

  if (dim == 2) {

    double  displ[2]; // (ux,uy) displacement

    for (int n = 0; n < mesh_struct.nnods_boundary ; n++ ) {
      strain_x_coord(strain_mac, &mesh_struct.boundary_coord[n*dim], displ);
      for (int d = 0; d < dim ; d++)
	x_arr[mesh_struct.boundary_indeces[n*dim + d]] = displ[d];
    }
  }

  VecRestoreArray(x_loc, &x_arr);
  VecGhostRestoreLocalForm(x, &x_loc);
  VecGhostUpdateBegin(x, INSERT_VALUES, SCATTER_FORWARD);
  VecGhostUpdateEnd(x, INSERT_VALUES, SCATTER_FORWARD);

  int nr_its = 0;
  double *b_arr;
  double norm = params.non_linear_min_norm_tol*10;

  VecNorm( x , NORM_2 , &norm );
  PRINTF2("|x| = %lf\n", norm);

  while (nr_its < params.non_linear_max_its && norm > params.non_linear_min_norm_tol) {

    save_event(MICRO_COMM, "ass_0");

    assembly_b_petsc();

    VecGetArray(b, &b_arr);
    for (int i = 0; i < mesh_struct.nnods_boundary * mesh_struct.dim; i++)
      b_arr[mesh_struct.boundary_indeces[i]] = 0.0;

    VecRestoreArray(b, &b_arr);
    VecGhostUpdateBegin(b, INSERT_VALUES, SCATTER_FORWARD);
    VecGhostUpdateEnd(b, INSERT_VALUES, SCATTER_FORWARD);

    VecNorm(b, NORM_2, &norm);
    PRINTF2("|b| = %lf \n", norm);

    if (norm < params.non_linear_min_norm_tol) break;

    VecScale(b, -1.0);

    assembly_A_petsc();

    MatZeroRowsColumns(A, mesh_struct.nnods_boundary * mesh_struct.dim, mesh_struct.boundary_indeces, 1.0, NULL, NULL);
    save_event(MICRO_COMM, "ass_1");

    save_event(MICRO_COMM, "sol_0");
    KSPSetOperators(ksp, A, A);
    KSPSolve(ksp, b, dx);
    save_event(MICRO_COMM, "sol_1");

    VecAXPY(x, 1.0, dx);
    VecGhostUpdateBegin(x, INSERT_VALUES, SCATTER_FORWARD);
    VecGhostUpdateEnd(x, INSERT_VALUES, SCATTER_FORWARD);

    nr_its ++;
  }
  save_event(MICRO_COMM, "ass_1");

  get_averages(strain_ave, stress_ave);

  if (flags.print_vectors == true) {
    PetscViewer viewer;
    PetscViewerASCIIOpen(MICRO_COMM,"b.dat" ,&viewer); VecView(b ,viewer);
    PetscViewerASCIIOpen(MICRO_COMM,"x.dat" ,&viewer); VecView(x ,viewer);
    PetscViewerASCIIOpen(MICRO_COMM,"dx.dat",&viewer); VecView(dx,viewer);
  }

  if (flags.print_matrices == true) {
    PetscViewer viewer;
    PetscViewerASCIIOpen(MICRO_COMM,"A.dat" ,&viewer); MatView(A ,viewer);
  }

  return 0;
}


int homogenize_init(void) {

  if (material_are_all_linear(&material_list) == true)
    flags.linear_materials = true;

  int ierr = 0;

  if (flags.linear_materials == true) {

    PRINTF1("calculating c tangent arround zero...\n")
    ierr = homogenize_calculate_c_tangent_around_zero(params.c_tangent_linear);

    flags.c_linear_calculated = true;

  }

  return ierr;
}


int homogenize_get_strain_stress(double *strain_mac, double *strain_ave, double *stress_ave) {

  if (flags.linear_materials == true && flags.c_linear_calculated == true) {

    for (int i = 0 ; i < nvoi ; i++) {
      strain_ave[i] = strain_mac[i];
      stress_ave[i] = 0.0;
      for (int j = 0 ; j < nvoi ; j++)
	stress_ave[i] += params.c_tangent_linear[i*nvoi + j] * strain_mac[j];
    }

  }
  else{

    int ierr = homogenize_get_strain_stress_non_linear(strain_mac, strain_ave, stress_ave);
    if (ierr) return 1;

  }
  return 0;
}


int homogenize_get_strain_stress_non_linear(double *strain_mac, double *strain_ave, double *stress_ave) {

  int ierr = 0;

  if (params.homog_method == HOMOG_METHOD_TAYLOR_PARALLEL || params.homog_method == HOMOG_METHOD_TAYLOR_SERIAL)
    ierr = mic_homogenize_taylor(strain_mac, strain_ave, stress_ave);

  else if (params.homog_method == HOMOG_METHOD_UNIF_STRAINS)
    ierr = mic_homog_us(strain_mac, strain_ave, stress_ave);

  return ierr;
}


int homogenize_get_c_tangent(double *strain_mac, double **c_tangent) {

  int ierr = 0;

  if (flags.linear_materials == true && flags.linear_materials == true)
    (*c_tangent) = params.c_tangent_linear;

  else if (flags.linear_materials == false) {

    ierr = homogenize_calculate_c_tangent(strain_mac, params.c_tangent);
    (*c_tangent) = params.c_tangent;
  }

  return ierr;
}


int homogenize_calculate_c_tangent_around_zero(double *c_tangent) {

  double strain_zero[MAX_NVOIGT];
  ARRAY_SET_TO_ZERO(strain_zero, nvoi);

  int ierr = homogenize_calculate_c_tangent(strain_zero, c_tangent);

  return ierr;
}


int homogenize_calculate_c_tangent(double *strain_mac, double *c_tangent) {

  double strain_1[MAX_NVOIGT], strain_2[MAX_NVOIGT];
  double stress_1[MAX_NVOIGT], stress_2[MAX_NVOIGT];
  double strain_aux[MAX_NVOIGT];

  PRINTF1("calc stress in ") PRINT_ARRAY("strain", strain_1, nvoi)
  ARRAY_COPY(strain_1, strain_mac, nvoi);

  int ierr = homogenize_get_strain_stress(strain_1, strain_aux, stress_1);

  for (int i = 0 ; i < nvoi ; i++) {

    PRINTF2("exp %d\n", i)
    ARRAY_COPY(strain_2, strain_mac, nvoi);

    strain_2[i] = strain_2[i] + HOMOGENIZE_DELTA_STRAIN;

    ierr = homogenize_get_strain_stress(strain_2, strain_aux, stress_2);

    for (int j = 0 ; j < nvoi ; j++)
      c_tangent[j*nvoi + i] = (stress_2[j] - stress_1[j]) / (strain_2[i] - strain_1[i]);

    if (flags.print_pvtu == true) {
      get_elem_properties();
      char filename[64];
      sprintf(filename,"micro_exp%d",i);
      ierr = micro_pvtu(filename);
      if (ierr != 0) {
	myio_printf(MICRO_COMM,"Problem writing vtu file\n");
	return ierr;
      }
    }

  }

  return ierr;
}


int strain_x_coord(double *strain, double *coord, double *u)
{
  if (dim == 2) {
    u[0] = strain[0]   * coord[0] + strain[2]/2 * coord[1] ;
    u[1] = strain[2]/2 * coord[0] + strain[1]   * coord[1] ;
  }
  return 0;
}


int assembly_b_petsc(void)
{
  double *b_arr;
  VecZeroEntries(b);
  VecGetArray(b, &b_arr);

  int npe = mesh_struct.npe;
  int dim = mesh_struct.dim;
  double *res_elem = malloc(dim*npe*sizeof(double));

  for (int e = 0 ; e < mesh_struct.nelm ; e++) {

    ARRAY_SET_TO_ZERO(res_elem, npe*dim);
    mesh_struct_get_elem_indeces(&mesh_struct, e, elem_index);

    for (int gp = 0; gp < ngp; gp++) {

      get_strain(e, gp, strain_gp);
      get_stress(e, gp, strain_gp, stress_gp);

      for (int i = 0; i < npe*dim; i++) {
	for (int j = 0; j < nvoi; j++)
	  res_elem[i] += struct_bmat[j][i][gp] * stress_gp[j] * struct_wp[gp];
      }

    }

    for (int i = 0; i < npe*dim; i++ )
      b_arr[elem_index[i]] += res_elem[i];
  }
  VecRestoreArray(b, &b_arr);

  return 0;
}


int assembly_A_petsc(void)
{
  MatZeroEntries(A);

  int npe = mesh_struct.npe;
  int dim = mesh_struct.dim;
  double *k_elem = malloc(dim*npe*dim*npe*sizeof(double));
  double *c = malloc(nvoi*nvoi*sizeof(double));

  for (int e = 0; e < mesh_struct.nelm; e++) {

    ARRAY_SET_TO_ZERO(k_elem, npe*dim*npe*dim)
    mesh_struct_get_elem_indeces(&mesh_struct, e, elem_index);

    for (int gp = 0; gp < ngp; gp++) {

      get_strain(e, gp, strain_gp);
      get_c_tan(NULL, e, gp, strain_gp, c);

      for (int i = 0 ; i < npe*dim; i++)
	for (int j = 0 ; j < npe*dim; j++)
	  for (int k = 0; k < nvoi ; k++)
	    for (int h = 0; h < nvoi; h++)
	      k_elem[ i*npe*dim + j] += struct_bmat[h][i][gp]*c[h*nvoi + k]*struct_bmat[k][j][gp]*struct_wp[gp];

    }
    MatSetValues(A, npe*dim, elem_index, npe*dim, elem_index, k_elem, ADD_VALUES);
  }

  MatAssemblyBegin(A, MAT_FINAL_ASSEMBLY);
  MatAssemblyEnd(A, MAT_FINAL_ASSEMBLY);

  return 0;
}


int get_averages(double *strain_ave, double *stress_ave)
{
  ARRAY_SET_TO_ZERO(strain_ave, nvoi);
  ARRAY_SET_TO_ZERO(stress_ave, nvoi);

  for (int e = 0 ; e < mesh_struct.nelm ; e++) {

    for (int gp = 0; gp < ngp; gp++) {

      get_strain(e, gp, strain_gp);
      get_stress(e, gp, strain_gp, stress_gp);

      for (int i = 0; i < nvoi; i++) {
	stress_ave[i] += stress_gp[i] * struct_wp[gp];
	strain_ave[i] += strain_gp[i] * struct_wp[gp];
      }
    }
  }

  for (int i = 0; i < nvoi; i++) {
    stress_ave[i] /= mesh_struct.vol;
    strain_ave[i] /= mesh_struct.vol;
  }
  return 0;
}


int get_elem_properties(void)
{
  for (int e = 0; e < mesh_struct.nelm; e++) {

    double strain_aux[MAX_NVOIGT];
    double stress_aux[MAX_NVOIGT];
    ARRAY_SET_TO_ZERO(strain_aux, nvoi);
    ARRAY_SET_TO_ZERO(stress_aux, nvoi);

    for (int gp = 0 ; gp < ngp ; gp++) {

      get_strain( e , gp, strain_gp );
      get_stress( e , gp, strain_gp, stress_gp );
      for (int v = 0 ; v < nvoi ; v++ ) {
	strain_aux[v] += strain_gp[v] * struct_wp[gp];
	stress_aux[v] += stress_gp[v] * struct_wp[gp];
      }

    }
    for (int v = 0 ; v < nvoi ; v++) {
      elem_strain[ e*nvoi + v ] = strain_aux[v] / mesh_struct.vol_elm;
      elem_stress[ e*nvoi + v ] = stress_aux[v] / mesh_struct.vol_elm;
    }
  }
  return 0;
}


int get_strain(int e, int gp, double *strain_gp)
{
  double *x_arr;
  VecGetArray(x, &x_arr);

  mesh_struct_get_elem_indeces(&mesh_struct, e, elem_index);

  for (int i = 0; i < mesh_struct.npe * mesh_struct.dim; i++)
    elem_disp[i] = x_arr[elem_index[i]];

  for (int v = 0; v < nvoi; v++) {
    strain_gp[v] = 0.0;
    for (int i = 0; i < mesh_struct.npe*dim; i++)
      strain_gp[v] += struct_bmat[v][i][gp] * elem_disp[i];
  }

  return 0;
}


int get_stress(int e, int gp, double *strain_gp, double *stress_gp)
{
  char *word_to_search;

  switch (elem_type[e]) {

    case ID_FIBER:
      word_to_search = strdup("FIBER");
      break;

    case ID_MATRIX:
      word_to_search = strdup("MATRIX");
      break;

    default:
      return 1;
  }

  material_t  *mat_p;
  node_list_t *pm = material_list.head;

  while (pm != NULL) {
    mat_p = (material_t *)pm->data;
    if (strcmp(mat_p->name, word_to_search) == 0) break;
    pm = pm->next;
  }

  if (pm == NULL) return 1;

  int ierr = material_get_stress(mat_p, dim, strain_gp, stress_gp);

  return ierr;
}


int get_c_tan(const char *name, int e, int gp, double *strain_gp, double *c_tan)
{
  char *word_to_search;

  if (name != NULL)
    word_to_search = strdup(name);
  else if ( elem_type[e] == ID_FIBER )
    word_to_search = strdup("FIBER");
  else if ( elem_type[e] == ID_MATRIX )
    word_to_search = strdup("MATRIX");

  material_t  *mat_p;
  node_list_t *pm = material_list.head;

  while (pm != NULL){
    mat_p = (material_t *)pm->data;
    if (strcmp(mat_p->name, word_to_search) == 0) break;
    pm = pm->next;
  }
  if (pm == NULL) return 1;

  material_get_c_tang(mat_p, dim, strain_gp, c_tan);

  return 0;
}


int get_elem_centroid(int e, int dim, double *centroid)
{
  if (dim == 2) {
    centroid[0] = (e % mesh_struct.nex + 0.5) * mesh_struct.hx;
    centroid[1] = (e / mesh_struct.nex + 0.5) * mesh_struct.hy;
  }
  return 0;
}


int init_shapes(double ***sh, double ****dsh, double **wp)
{
  int nsh = (dim == 2) ? 4 : 8;
  double *xp = malloc(ngp*dim*sizeof(double));

  if (mesh_struct.dim == 2) {
    xp[0] = -0.577350269189626;   xp[1]= -0.577350269189626;
    xp[2] = +0.577350269189626;   xp[3]= -0.577350269189626;
    xp[4] = +0.577350269189626;   xp[5]= +0.577350269189626;
    xp[6] = -0.577350269189626;   xp[7]= +0.577350269189626;
  }

  *sh = malloc(nsh*sizeof(double*));
  for (int is = 0; is < nsh; is++) {
    (*sh)[is] = malloc( ngp * sizeof(double));
  }

  *dsh  = malloc(nsh*sizeof(double**));
  for (int is = 0; is < nsh; is++ ) {
    (*dsh)[is] = malloc(dim * sizeof(double*));
    for (int d = 0; d < dim; d++) {
      (*dsh)[is][d] = malloc( ngp * sizeof(double));
    }
  }
  *wp= malloc(ngp*sizeof(double));

  if (dim == 2){

    double hx = mesh_struct.hx;
    double hy = mesh_struct.hy;
    for (int gp = 0; gp < ngp; gp++) {
      (*sh)[0][gp] = (1 - xp[2*gp]) * (1 - xp[2*gp+1])/4;
      (*sh)[1][gp] = (1 + xp[2*gp]) * (1 - xp[2*gp+1])/4;
      (*sh)[2][gp] = (1 + xp[2*gp]) * (1 + xp[2*gp+1])/4;
      (*sh)[3][gp] = (1 - xp[2*gp]) * (1 + xp[2*gp+1])/4;
    }

    for (int gp = 0; gp < ngp; gp++) {
      (*dsh)[0][0][gp] = -1 * (1 - xp[2*gp+1]) /4 * 2/hx; // d phi / d x
      (*dsh)[1][0][gp] = +1 * (1 - xp[2*gp+1]) /4 * 2/hx;
      (*dsh)[2][0][gp] = +1 * (1 + xp[2*gp+1]) /4 * 2/hx;
      (*dsh)[3][0][gp] = -1 * (1 + xp[2*gp+1]) /4 * 2/hx;
      (*dsh)[0][1][gp] = -1 * (1 - xp[2*gp+0]) /4 * 2/hy; // d phi / d y
      (*dsh)[1][1][gp] = -1 * (1 + xp[2*gp+0]) /4 * 2/hy;
      (*dsh)[2][1][gp] = +1 * (1 + xp[2*gp+0]) /4 * 2/hy;
      (*dsh)[3][1][gp] = +1 * (1 - xp[2*gp+0]) /4 * 2/hy;
    }


    for (int gp = 0 ; gp < ngp ; gp++)
      (*wp)[gp] = mesh_struct.vol_elm / ngp;

  }

  free(xp);

  return 0;
}

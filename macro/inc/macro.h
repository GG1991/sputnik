#ifndef MACRO_H
#define MACRO_H

#include "comm.h"
#include "util.h"
#include "function.h"
#include "vtk.h"
#include "myio.h"
#include "gmsh.h"
#include "mesh.h"
#include "fem.h"
#include "solvers.h"
#include "material.h"

#define GREEN "\x1B[32m"
#define RED "\x1B[31m"
#define NORMAL "\x1B[0m"

#define CALC_MODE_NULL   0
#define CALC_MODE_NORMAL 1
#define CALC_MODE_EIGEN  2
#define CALC_MODE_TEST   3

#define NBUF 256

double **struct_sh;
double ***struct_dsh;
double *struct_wp;
double ***bmat;
int npe_max;
int ngp_max;
double *elem_disp;
double *elem_coor;
int *loc_elem_index;
int *glo_elem_index;
double *strain_gp;
double *stress_gp;
int *elem_type;
double *elem_strain;
double *elem_stress;
double *elem_energy;
double *res_elem;
double *k_elem;
double *m_elem;
double *c;
double ***dsh;
double *detj;
double **jac;
double **jac_inv;

int nvoi;
int dim;

int rank_mac;
int nproc_mac;
int nproc_wor;
int rank_wor;

int flag_neg_detj;

char mesh_n[128];

typedef struct{

  int calc_mode;

  int non_linear_max_its;
  double non_linear_min_norm_tol;

  int num_eigen_vals;
  double *eigen_vals;

  double tf;
  double dt;
  double t;
  int ts;

  double energy_stored;
  int non_linear_its;
  double residual_norm;

}params_t;

typedef struct{

  bool coupled;
  bool allocated;
  bool print_pvtu;
  bool print_vectors;
  bool print_matrices;

}flags_t;

extern params_t params;
extern flags_t flags;

int assembly_A(void);
int assembly_b(double *norm);
int assembly_AM(void);
int assembly_get_global_elem_index(int e, int *glo_elem_index);
int assembly_get_local_elem_index(int e, int *loc_elem_index);
int assembly_get_elem_properties(void);
int assembly_get_strain(int e , int gp, int *loc_elem_index, double ***dsh, double ***bmat, double *strain_gp);
int assembly_get_stress(int e , int gp, double *strain_gp , double *stress_gp);
int assembly_get_c_tan(const char * name, int e, int gp, double *strain_gp, double *c_tan);
int assembly_get_rho(const char * name, int e, double *rho);
int assembly_get_sh(int dim, int npe, double ***sh);
int assembly_get_dsh(int e, int * loc_elem_index, double ***dsh, double *detj);
int assembly_get_wp(int dim, int npe, double **wp);
int assembly_get_bmat(int e, double ***dsh, double ***bmat);
int assembly_get_mat_name(int id, char * name_s);

int macro_pvtu(char *name);
int update_boundary(double t, list_t *function_list, list_t *boundary_list);
int read_coord(char *mesh_n, int nmynods, int *mynods, int nghost, int *ghost, double **coord);

void init_variables(void);
int finalize(void);

int alloc_memory(void);

int copy_gmsh_to_mesh(gmsh_mesh_t *gmsh_mesh, mesh_t *mesh);

int boundary_read(void);
int boundary_update(double time);
int boundary_setx(void);

int comm_line_set_flags(void);

Mat A;
Mat M;
Vec x, dx, b;


#endif

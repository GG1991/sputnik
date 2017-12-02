#ifndef MACRO_H
#define MACRO_H

#include "sputnik.h"
#include "comm.h"
#include "util.h"
#include "gmsh.h"
#include "function.h"
#include "vtk.h"
#include "myio.h"
#include "mesh.h"

#define NORMAL      1
#define EIGENSYSTEM 2
#define TEST_COMM   3

#define NBUF        256          // buffer length

int         macro_mode;

int         nr_max_its;          // newton raphson maximum number of iterations
double      nr_norm_tol;         // newton raphson minimum tolerance

double    **struct_sh;           // Shape functions
double   ***struct_dsh;          // Derivative shapes functions
double     *struct_wp;           // Weights
double   ***bmat;                // B matrix (Bu = epsilon)
int         npe_max;             // maximum number of nodes per element
int         ngp_max;             // maximum number of gauss points
double     *elem_disp;           // elemental displacements
double     *elem_coor;           // coordinates of the element's verteces
int        *loc_elem_index;      // local elemental index vector for assembly and reading
int        *glo_elem_index;      // global elemental index vector for assembly and reading
double     *strain_gp;           // strain at Gauss point
double     *stress_gp;           // stress at Gauss point
int        *elem_type;           // type in each element
double     *elem_strain;         // strain at each element
double     *elem_stress;         // stress at each element
double     *elem_energy;         // energy at each element
double     *res_elem;            // elemental residue
double     *k_elem;              // elemental steffiness matrix
double     *m_elem;              // elemental mass matrix
double     *c;                   // constitutive tensor
double   ***dsh;                 // shape functions derivatives at gauss points
double     *detj;                // jacobian determinants for each gauss point
double    **jac;                 // elemental jacobian
double    **jac_inv;             // inverse of elemental jacobian

int         nvoi;                // voigt number

int         mymicro_rank_worker;
int         flag_neg_detj;       // negative jacobian flag
int         nnz_factor;          // non zeros factor to multiply the default

int         rank_mac;            // rank on macro comm
int         nproc_mac;           // # of macro processes (WORLD_COMM)  

char        filename[NBUF];      // string for different purposes
char       *mesh_n;        // mesh path;
int         mesh_f;              // mesh format

Mat         A;                   // steffiness matrix          
Mat         M;                   // mass matrix
Vec         x, dx, b;            // vectors unknowns and RHS 

typedef struct eigen_mode_t_
{
  int       nev;
  double   *eigen_vals;          // eigenvalues
  double    energy;              // elastic energy to perform normalization

}eigen_mode_t;

typedef struct normal_mode_t_
{
  double    tf;                  // final time
  double    dt;                  // time step

}normal_mode_t;

normal_mode_t normal_mode;
eigen_mode_t  eigen_mode;

list_t      boundary_list;
list_t      physical_list;

#endif

/*

A port of part of Greg Turk's reaction-diffusion code, from:
http://www.cc.gatech.edu/~turk/reaction_diffusion/reaction_diffusion.html

See README.txt for more details.

*/

// hardware
#if (defined(__i386__) || defined(__amd64__) || defined(__x86_64__) || 	defined(_M_X64) || defined(_M_IX86))
# include <xmmintrin.h>
#endif

// To convince yourself that the macro library works on any hardware,
// un-comment this "#define HWIV_EMULATE", and you'll get the macros inside
// the #ifdef HWIV_V4F4_EMULATED block in hwi_vector.h.  The emulated
// macros do everything with normal floats and arrays, and run about 3-4 times
// slower.
//#define HWIV_EMULATE
#define HWIV_WANT_V4F4
#include "hwi_vector.h";

// stdlib:
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
    #include <sys/timeb.h>
    #include <sys/types.h>
    #include <winsock.h>
    // http://www.linuxjournal.com/article/5574
    void gettimeofday(struct timeval* t,void* timezone)
    {       struct _timeb timebuffer;
          _ftime( &timebuffer );
          t->tv_sec=timebuffer.time;
          t->tv_usec=1000*timebuffer.millitm;
    }
#else
    #include <sys/time.h>
#endif

// local:
#include "display_hwiv.h"

static int g_width = 256;
static int g_height = 256;
#define INDEX(a,x,y) ((a)[(x)*g_width+(y)])

float *allocate(int width, int height, const char * error_text);
float *allocate(int width, int height, const char * error_text)
{
  size_t sz;
  float * rv;
  sz = ((size_t) width) * ((size_t) height) * sizeof(*rv);
  rv = (float *) malloc(sz);
  if (rv == NULL) {
    fprintf(stderr, "allocate: Could get %ld bytes for %s\n", ((long) sz),
      error_text);
    exit(-1);
  }
  return (rv);
}

void init(float *a, float *b,
          int density, int lowback, int BZrects);

void compute(float *a, float *b, float *da, float *db,
             float D_u, float D_v, float F, float k,
             float speed,
             int parameter_space);

void colorize(float *u, float *v, float *du,
             float *red, float *green, float *blue);

static int g_color = 0;
static int g_paramspace = 0;
static int g_wrap = 0;
static float g_k, g_F;
static int g_ramprects = 0;
static int g_lowback = 0;
static float g_scale = 1.0;
static int g_density = 0;

int main(int argc, char * * argv)
{
  // Here we implement the Gray-Scott model, as described here:
  // http://arxiv.org/abs/patt-sol/9304003
  //    (the seminal paper by Pearson)
  // http://www.cc.gatech.edu/~turk/bio_sim/hw3.html
  //    (a present university course project, by Greg Turk at Georgia Tech)
  // http://www.mrob.com/pub/comp/xmorphia/index.html
  //    (a web exhibit with over 100 videos and 500 images)

  // -- parameters --
  float D_u = 0.082;
  float D_v = 0.041;

     g_k = 0.064;  g_F = 0.035;  // spots
  // g_k = 0.059;  g_F = 0.022;  // spots that keep killing each other off
  // g_k = 0.06;   g_F = 0.035;  // stripes
  // g_k = 0.065;  g_F = 0.056;  // long stripes
  // g_k = 0.064;  g_F = 0.04;   // dots and stripes
  // g_k = 0.0475; g_F = 0.0118; // spiral waves
  // g_k = 0.059;  g_F = 0.094;  // "soap bubbles"
  float speed = 1.0;

  bool custom_Fk = false;

  for (int i = 1; i < argc; i++) {
    if (0) {
    } else if (strcmp(argv[i],"-color")==0) {
      // do output in wonderful technicolor
      g_color = 1;
    } else if ((i+1<argc) && (strcmp(argv[i],"-density")==0)) {
      // set density of spots for initial pattern
      i++; g_density = atoi(argv[i]);
    } else if ((i+1<argc) && 
         ((strcmp(argv[i],"-F")==0) || (strcmp(argv[i],"-f")==0)) ) {
      // set parameter F
      i++; g_F = atof(argv[i]);
      custom_Fk = true;
    } else if ((i+1<argc) && (strcmp(argv[i],"-height")==0)) {
      // set height
      i++; g_height = atoi(argv[i]);
    } else if ((i+1<argc) && (strcmp(argv[i],"-k")==0)) {
      // set parameter k
      i++; g_k = atof(argv[i]);
      custom_Fk = true;
    } else if (strcmp(argv[i],"-paramspace")==0) {
      // do a parameter space plot, like in the Pearson paper
      g_paramspace = 1;
    } else if (strcmp(argv[i],"-ramprects")==0) {
      // use ramped rectangles in the initial pattern
      g_ramprects = 1;
    } else if (strcmp(argv[i],"-lowback")==0) {
      // use "low" background value in init
      g_lowback = 1;
    } else if ((i+1<argc) && (strcmp(argv[i],"-scale")==0)) {
      // set scale
      i++; g_scale = atof(argv[i]);
    } else if ((i+1<argc) && (strcmp(argv[i],"-size")==0)) {
      // set both width and height
      i++; g_height = g_width = atoi(argv[i]);
    } else if ((i+1<argc) && (strcmp(argv[i],"-width")==0)) {
      // set width
      i++; g_width = atoi(argv[i]);
    } else if (strcmp(argv[i],"-wrap")==0) {
      // patterns wrap around ("torus", also called "continuous boundary
      // condition")
      g_wrap = 1;
    } else {
      fprintf(stderr, "Unrecognized argument: '%s'\n", argv[i]);
      exit(-1);
    }
  }

  if ((g_scale == 1.0) && (custom_Fk)) {
    g_scale = 2.0;
  }
  D_u = D_u * g_scale;
  D_v = D_v * g_scale;
  speed = speed / g_scale;

  if (g_width%4) {
    g_width = ((g_width/4)+1)*4;
  }

#if (defined(__i386__) || defined(__amd64__) || defined(__x86_64__) || 	defined(_M_X64) || defined(_M_IX86))
  /* On Intel we disable accurate handling of denorms and zeros. This is an
     important speed optimization. */
  int oldMXCSR = _mm_getcsr(); //read the old MXCSR setting
  int newMXCSR = oldMXCSR | 0x8040; // set DAZ and FZ bits
  _mm_setcsr( newMXCSR ); //write the new MXCSR setting to the MXCSR
#endif

  // ----------------
  
  // these arrays store the chemical concentrations:
  float *u = 0;
  float *v = 0;
  // these arrays store the rate of change of those chemicals:
  float *du = 0;
  float *dv = 0;

  float *red = 0;
  float *green = 0;
  float *blue = 0;

  u = allocate(g_width, g_height, "U array");
  v = allocate(g_width, g_height, "V array");
  du = allocate(g_width, g_height, "D_u array");
  dv = allocate(g_width, g_height, "D_v array");
  red = allocate(g_width, g_height, "red array");
  green = allocate(g_width, g_height, "green array");
  blue = allocate(g_width, g_height, "blue array");

  // put the initial conditions into each cell
  init(u,v, g_density, g_lowback, g_ramprects);

  int N_FRAMES_PER_DISPLAY;

#ifdef HWIV_EMULATE
  N_FRAMES_PER_DISPLAY = 500;
#else
  N_FRAMES_PER_DISPLAY = 2000;
#endif

  int iteration = 0;
  double fps_avg = 0.0; // decaying average of fps
  while(true) 
  {
    struct timeval tod_record;
    double tod_before, tod_after, tod_elapsed;
    double fps = 0.0;     // frames per second

    gettimeofday(&tod_record, 0);
    tod_before = ((double) (tod_record.tv_sec))
                                + ((double) (tod_record.tv_usec)) / 1.0e6;

    // compute:
    for(int it=0;it<N_FRAMES_PER_DISPLAY;it++)
    {
      compute(u,v,du,dv,D_u,D_v,g_F,g_k,speed,g_paramspace);
      iteration++;
    }

    if (g_color) {
      colorize(u, v, du, red, green, blue);
    }

    gettimeofday(&tod_record, 0);
    tod_after = ((double) (tod_record.tv_sec))
                                + ((double) (tod_record.tv_usec)) / 1.0e6;

    tod_elapsed = tod_after - tod_before;
    fps = ((double)N_FRAMES_PER_DISPLAY) / tod_elapsed;
    // We display an exponential moving average of the fps measurement
    fps_avg = (fps_avg == 0) ? fps : (((fps_avg * 10.0) + fps) / 11.0);

    char msg[1000];
    sprintf(msg,"GrayScott - %0.2f fps", fps_avg);

    // display:
    {
      int chose_quit;
      if (g_color) {
        chose_quit = display(g_width,g_height,red,green,blue,iteration,false,200.0f,2,10,msg);
      } else {
        chose_quit = display(g_width,g_height,u,u,u,iteration,false,200.0f,2,10,msg);
      }
      if (chose_quit) // did user ask to quit?
        break;
    }
  }
}

#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

// return a random value between lower and upper
float frand(float lower,float upper)
{
  float rv;

  rv = lower + rand()*(upper-lower)/RAND_MAX;
  rv = max(0.0, min(1.0, rv));
  return rv;
}

/* pearson_bkg fills everything with the trivial state (U=1, V=0) combined
   with random noise of magnitude 0.01, and it does this while keeping all
   U and V values between 0 and 1. */
void pearson_bkg(float *u, float *v)
{
  int i, j;
  for(i=0; i<g_height; i++) {
    for(j=0; j<g_width; j++) {
      INDEX(u,i,j) = frand(0.99, 1.0);
      INDEX(v,i,j) = frand(0.0, 0.01);
    }
  }
}

/* pearson_block creates a block filled with a given U and V value combined
   with superimposed noise of amplitude 0.01 */
void pearson_block(float *u, float *v,
                 int vpos, int hpos, int h, int w, float U, float V)
{
  int i, j;

  for(i=vpos; i<vpos+h; i++) {
    if ((i >= 0) && (i < g_height)) {
      for(j=hpos; j<hpos+w; j++) {
        if ((j >= 0) && (j < g_width)) {
          INDEX(u,i,j) = frand(U-0.005, U+0.005);
          INDEX(v,i,j) = frand(V-0.005, V+0.005);
        }
      }
    }
  }
}

/* ramp_block creates a starting pattern for B-Z spirals and continuous
   propagating wave fronts (pattern type xi in my paper). */
void ramp_block(float *u, float *v,
                int vpos, int hpos, int h, int w, int fliph)
{
  int i, j;
  float U, V;

  for(i=vpos; i<vpos+h; i++) {
    if ((i >= 0) && (i < g_height)) {
      for(j=hpos; j<hpos+w; j++) {
        if ((j >= 0) && (j < g_width)) {
          U = ((float) (j-hpos)) / ((float) w);
          if (fliph) {
            U = 1.0 - U;
          }
          V = U;

          if (U < 0.1) {
            U = U / 0.1;
          } else {
            U = (1.0 - U) / 0.9;
          }
          U = 1.0 - sin((1.0 - U) * 1.5708);
          U = 1.0 - (U * 0.95); // formerly 0.85
          
          if (V < 0.08) { // formerly 0.05
            V = V / 0.08; // formerly 0.05
          } else if (V < 0.5) {
            V = (0.5 - V) / 0.42; // formerly 0.45
          } else {
            V = 0;
          }
          V = 1.0 - sin((1.0 - V) * 1.5708);
          V = V * 0.4;

          INDEX(u,i,j) = frand(U-0.005, U+0.005);
          INDEX(v,i,j) = frand(V-0.005, V+0.005);
        }
      }
    }
  }
}

/* Given an F and k, returns the U and V values for the homogeneous state at
 that F and k. Tries to return the secondary (blue) state, but if that
 doesn't exist it returns the trivial (red) state.
   The formula is derived from Muratov and Osipov 2000 formula 2.12 (with A
 defined by 2.10, and the other variables defined as in 2.3, 2.4 and 2.5 (all
 of this is on pages 8-9 of "Muratov 2000 Spike.pdf").
   There is a more obvious formula (expressed in terms of F and k) in
 "Leppanen 2004 Computational.pdf" page 40 (his equation 3.22).
   To get the values for the center F and k settings, use (g_F_CTR, g_k_CTR) */
void homogen_uv(float F, float k, float * hU, float * hV)
{
  float sqrt_F, A;
  float U, V;

  sqrt_F = sqrt(F);

  if (k < (sqrt_F - 2.0 * F) / 2.0) {
    A = sqrt_F / (F + k);
    U = (A - sqrt(A*A - 4.0)) / (2.0 * A);
    U = max(0.0, min(1.0, U));
    V = sqrt_F * (A + sqrt(A*A - 4.0)) / 2.0;
    V = max(0.0, min(1.0, V));
  } else {
    U = 1.0;
    V = 0.0;
  }
  *hU = U;
  *hV = V;
}

/* i5_bkg is the background for my init routines. It fills the space either
   with the trivial stable state (V=0, U=1), or when the other stable "blue"
   state exists it uses that state. This makes for much more interesting
   initial patterns for the areas near the various bifurcation lines and for
   Uskate world. */
void i5_bkg(float *u, float *v, int which)
{
  int i, j;

  for(i=0; i<g_height; i++) {
    for(j=0; j<g_width; j++) {
      float U, V;

      if (which == 0) {
        U = 1.0; V = 0.0;
      } else {
        homogen_uv(g_F, g_k, &U, &V);
      }
      // printf("F %g  k %g  A %g  U %g  V %g\n", F, k, A, U, V);
      INDEX(u,i,j) = U;
      INDEX(v,i,j) = V;
    }
  }
}

void init(float *u, float *v,
          int density, int lowback, int BZrects)
{
  long nsp, i;
  long base, var;

  srand((unsigned int)time(NULL));

  if (density <= 0) {
    nsp = 0;
  } else {
    if (density == 3) {
      base = (g_height * g_width) / 512;
      var = 2;
    } else {
      base = density * 20;
      var = density * (g_height * g_width) / 1000;
    }

    nsp = base + (rand() % var);
    printf("Adding %ld random rectangles\n", nsp);
  }

  i5_bkg(u, v, lowback ? 0 : 1);

  for(i=0; i<nsp; i++) {
    int v1, vs, h1, hs;
    float U, V;

    vs = 10 + (rand() % 12) * (rand() % 12) / 5;
    hs = 10 + (rand() % 12) * (rand() % 12) / 5;

    v1 = (rand() % g_height) - (vs / 2);
    h1 = (rand() % g_width) - (hs / 2);

    U = frand(0.0, 1.0);
    if (BZrects) {
      V = frand(0.0, 1.0 - U);
    } else {
      V = frand(0.0, 1.0);
    }
    if (BZrects) {
      if ((rand() & 0x3) == 0) {
        int h;
        h = 40 + (rand() & 31); // formerly 20 + ...
        ramp_block(u, v, v1, h1, h, 100, rand() & 1); // formerly 60
      } else {
        pearson_block(u, v, v1, h1, vs, hs, U, V);
      }
    } else {
      pearson_block(u, v, v1, h1, vs, hs, U, V);
    }
  }
    
  if (nsp <= 0) {
  // old init pattern
  for(int i = 0; i < g_height; i++) {
    for(int j = 0; j < g_width; j++) {
      if(hypot(i-g_height/2,(j-g_width/2)/1.5)<=frand(2,5)) // start with a uniform field with an approximate circle in the middle
      {
        INDEX(u,i,j) = frand(0.0,0.1);
        INDEX(v,i,j) = frand(0.9,1.0);
      }
    }
  }
  }
}

void compute(float *u, float *v, float *du, float *dv,
             float D_u,float D_v,float F,float k,float speed,
             int parameter_space)
{
#ifndef HWIV_HAVE_V4F4
  fprintf(stdout, "Did not get vector macros from HWIV\n");
  exit(-1);
#endif
  V4F4 v4_speed; // vectorized version of speed scalar
  V4F4 v4_F; // vectorized version of F scalar
  V4F4 v4_k; // vectorized version of k scalar
  HWIV_4F4_ALIGNED talign; // used by FILL_4F4
  V4F4 v4_u; V4F4 v4_du;
  V4F4 v4_v; V4F4 v4_dv;
  HWIV_INIT_MUL0_4F4; // used by MUL (on targets that need it)
  HWIV_INIT_MTMP_4F4; // used by MADD (on targets that need it)
  HWIV_INIT_FILL;     // used by FILL
  HWIV_INIT_RLTMP_4F4; // used by RAISE and LOWER
  V4F4 v4_tmp;
  V4F4 v4_Du;
  V4F4 v4_Dv;
  V4F4 v4_nabla_u;
  V4F4 v4_nabla_v;
  V4F4 v4_1;
  V4F4 v4_4;
  const float k_min=0.045f, k_max=0.07f, F_min=0.01f, F_max=0.09f;
  float k_diff;
  V4F4 v4_kdiff;
  float * ubase; float * ub_prev; float * ub_next;
  float * vbase; float * vb_prev; float * vb_next;
  float * dubase; float * dvbase;

  //F_diff = (F_max-F_min)/g_width;
  k_diff = (k_max-k_min)/g_height;

  // Initialize our vectorized scalars
  HWIV_SPLAT_4F4(v4_speed, speed);
  HWIV_SPLAT_4F4(v4_F, F);
  HWIV_SPLAT_4F4(v4_k, k);
  HWIV_SPLAT_4F4(v4_Du, D_u);
  HWIV_SPLAT_4F4(v4_Dv, D_v);
  HWIV_SPLAT_4F4(v4_1, 1.0);
  HWIV_SPLAT_4F4(v4_4, 4.0);
  HWIV_FILL_4F4(v4_kdiff, 0, -k_diff, -2*k_diff, -3*k_diff);

  // Scan per row
  for(int i = 0; i < g_height; i++) {
    int iprev,inext;
    if (g_wrap) {
      iprev = (i+g_height-1) % g_height;
      inext = (i+1) % g_height;
    } else {
      iprev = max(i-1, 0);
      inext = min(i+1, g_height-1);
    }
    /* Get pointers to beginning of rows for each of the grids. We access
       3 rows each for u and v, and 1 row each for du and dv. */
    ubase = &INDEX(u,i,0);
    ub_prev = &INDEX(u,iprev,0);
    ub_next = &INDEX(u,inext,0);
    vbase = &INDEX(v,i,0);
    vb_prev = &INDEX(v,iprev,0);
    vb_next = &INDEX(v,inext,0);
    dubase = &INDEX(du,i,0);
    dvbase = &INDEX(dv,i,0);

    if (parameter_space) {
      // set F for this row (ignore the provided value)
      F = F_min + (g_height-i-1) * (F_max-F_min)/g_width;
      HWIV_SPLAT_4F4(v4_F, F);
    }

    // Scan per column in steps of vector width
    for(int j = 0; j < g_width; j+=4) {
      int jprev,jnext;

      if (g_wrap) {
        jprev = (j+g_width-4) % g_width;
        jnext = (j+4) % g_width;
      } else {
        jprev = max(j-4, 0);
        jnext = min(j+4, g_height-4);
      }

      // float uval = u[i][j];
      HWIV_LOAD_4F4(v4_u, ubase+j);
      // float vval = v[i][j];
      HWIV_LOAD_4F4(v4_v, vbase+j);

      if (parameter_space) {
        // set k for this column (ignore the provided value)
        k = k_min + (g_width-j-1)*k_diff;
        // k decreases by k_diff each time j increases by 1, so this vector
        // needs to contain 4 different k values.
        HWIV_SPLAT_4F4(v4_tmp, k);
        HWIV_ADD_4F4(v4_k, v4_tmp, v4_kdiff);
      }

      // compute the Laplacians of u and v. "nabla" is the name of the
      // "upside down delta" symbol used for the Laplacian in equations

      /* Scalar code is:
         nabla_u = u[i][jprev]+u[i][jnext]+u[iprev][j]+u[inext][j] - 4*uval;

         The first two are hard to load because they require unaligned access.
         We instead need to load a whole vector from the neighboring location
         and shift (RAISE or LOWER) the contents by one position. (N.B.: This
         is the one part of vector computation that is pretty much guaranteed
         to confuse even experienced vector programmers.)

         Assuming j increases as you move to the right, consider the case of
         getting the 4 "left neighbors" into a vector. If we're doing the
         computation for pixels (4,5,6,7) then j=4. To get the "left neighbors"
         we need to get (3,4,5,6). Ideally we would just do this:

                           j-1
                            v______
                      0 1 2{3 4 5 6}7 8 9 10 11
                             \ \ \ \
                              v v v v
                             +-------+
                             |vector |
                             +-------+

         But you can't load a vector from position 3, it has to be loaded from
         a multiple of 4. So instead, we load two vectors like this:

                    jprev     j     jnext
                      v______ v______ v
                     {0 1 2 3|4 5 6 7}8 9 10 11
                      | | | | | | | |
                      v v v v v v v v
                     +-------+-------+
                     | vectr | vectr |
                     +-------+-------+

         And then "shift" the data within the pair of vectors.

         Although this diagram, and the arrangement of pixels on the screen
         makes this look like a "right shift", on Intel and other
         little-endian machines, when these memory locations get loaded into
         the vectors they actually end up in the vectors arranged like this:

                     +-------+-------+
                     |3 2 1 0|7 6 5 4| Little-endian: first element of memory
                     +-------+-------+ ends up in "right" end of register!

         We still want to shift element 3 into the vector containing 4,5,6,7
         and have the 4,5,6 move over one spot with the 7 getting lost.
         So instead of "left" or "right", think of it as moving the
         data "up", because each datum moves to the next higher-numbered
         position.                                                        */
      HWIV_LOAD_4F4(v4_du, ubase+jprev);
      HWIV_RAISE_4F4(v4_nabla_u, v4_u, v4_du);

      // Similar operation to get "right neighbor". This time the data
      // from locations (x+1, x+2, x+3, x+4) gets "lowered" one step to
      // positions (x, x+1, x+2, x+3). The "x+4" element has to come from
      // the block of 4 values to the right of the current block, and jnext
      // points to that block.
      HWIV_LOAD_4F4(v4_du, ubase+jnext);
      HWIV_LOWER_4F4(v4_tmp, v4_u, v4_du);
      HWIV_ADD_4F4(v4_nabla_u, v4_nabla_u, v4_tmp);

      // Now we add in the "up" and "down" neighbors
      HWIV_LOAD_4F4(v4_tmp, ub_prev+j);
      HWIV_ADD_4F4(v4_nabla_u, v4_nabla_u, v4_tmp);
      HWIV_LOAD_4F4(v4_tmp, ub_next+j);
      HWIV_ADD_4F4(v4_nabla_u, v4_nabla_u, v4_tmp);

      // Now we compute -(4*u-neighbors)  = neighbors - 4*u
      HWIV_NMSUB_4F4(v4_nabla_u, v4_4, v4_u, v4_nabla_u);

      // Same thing all over again for the v's
      HWIV_LOAD_4F4(v4_dv, vbase+jprev);
      HWIV_RAISE_4F4(v4_nabla_v, v4_v, v4_dv);
      HWIV_LOAD_4F4(v4_dv, vbase+jnext);
      HWIV_LOWER_4F4(v4_tmp, v4_v, v4_dv);
      HWIV_ADD_4F4(v4_nabla_v, v4_nabla_v, v4_tmp);
      HWIV_LOAD_4F4(v4_tmp, vb_prev+j);
      HWIV_ADD_4F4(v4_nabla_v, v4_nabla_v, v4_tmp);
      HWIV_LOAD_4F4(v4_tmp, vb_next+j);
      HWIV_ADD_4F4(v4_nabla_v, v4_nabla_v, v4_tmp);
      HWIV_NMSUB_4F4(v4_nabla_v, v4_4, v4_v, v4_nabla_v);

      // compute the new rate of change of u and v

      /* Scalar code is:
           du[i][j] = D_u * nabla_u - uval*vval*vval + F*(1-uval);
         We treat it as:
                      D_u * nabla_u - (uval*vval*vval - (-(F*uval-F)) ) */

      HWIV_NMSUB_4F4(v4_tmp, v4_F, v4_u, v4_F);        // -(F*u-F) = F-F*u = F(1-u)
      HWIV_MUL_4F4(v4_dv, v4_v, v4_v);                 // v^2
      HWIV_MSUB_4F4(v4_tmp, v4_u, v4_dv, v4_tmp);      // u*v^2 - F(1-u)
      HWIV_MSUB_4F4(v4_du, v4_Du, v4_nabla_u, v4_tmp); // D_u*nabla_u - (u*v^2 - F(1-u))
                                                       // = D_u*nabla_u - u*v^2 + F(1-u)
      HWIV_SAVE_4F4(dubase+j, v4_du);

      /* dv formula is similar:
           dv[i][j] = D_v * nabla_v + uval*vval*vval - (F+k)*vval;
         We treat it as:
                      D_v * nabla_v + uval*vval*vval - (F*vval + k*vval); */
      HWIV_MUL_4F4(v4_tmp, v4_k, v4_v);                // k*v
      HWIV_MADD_4F4(v4_tmp, v4_F, v4_v, v4_tmp);       // F*v+k*v = (F+k)v
      // HWIV_MUL_4F4(v4_dv, v4_v, v4_v);                 v^2 (unchanged from above)
      HWIV_MSUB_4F4(v4_tmp, v4_u, v4_dv, v4_tmp);      // u*v^2 - (F+k)v
      HWIV_MADD_4F4(v4_dv, v4_Dv, v4_nabla_v, v4_tmp); // D_v*nabla_v + u*v^2 - (F+k)v
      HWIV_SAVE_4F4(dvbase+j, v4_dv);
    }
  }

  // effect change
    for(int i = 0; i < g_height; i++) {
      ubase = &INDEX(u,i,0);
      vbase = &INDEX(v,i,0);
      dubase = &INDEX(du,i,0);
      dvbase = &INDEX(dv,i,0);
      for(int j = 0; j < g_width; j+=4) {
        // u[i][j] = u[i][j] + speed * du[i][j];
        HWIV_LOAD_4F4(v4_u, ubase);            // get u
        HWIV_LOAD_4F4(v4_du, dubase);          // get du
        HWIV_MADD_4F4(v4_u, v4_speed, v4_du, v4_u); // speed*du + u
        HWIV_SAVE_4F4(ubase, v4_u);            // write it back
        ubase += 4; dubase += 4;

        // v[i][j] = v[i][j] + speed * dv[i][j];
        HWIV_LOAD_4F4(v4_v, vbase);
        HWIV_LOAD_4F4(v4_dv, dvbase);
        HWIV_MADD_4F4(v4_v, v4_speed, v4_dv, v4_v);
        HWIV_SAVE_4F4(vbase, v4_v);
        vbase += 4; dvbase += 4;
      }
    }
}

void colorize(float *u, float *v, float *du,
             float *red, float *green, float *blue)
{
  // Step by row
  for(int i = 0; i < g_height; i++) {
    // step by column
    for(int j = 0; j < g_width; j++) {
      float uval = INDEX(u,i,j);
      float vval = INDEX(v,i,j);
      float delta_u = (INDEX(du,i,j) * 1000.0f) + 0.5f;
      delta_u = ((delta_u < 0) ? 0.0 : (delta_u > 1.0) ? 1.0 : delta_u);

      // Something simple to start (-:
      // different colour schemes result if you reorder these, or replace
      // "x" with "1.0f-x" for any of the 3 variables
      INDEX(red,i,j) = delta_u; // increasing U will look pink
      INDEX(green,i,j) = 1.0-uval;
      INDEX(blue,i,j) = 1.0-vval;
    }
  }
}
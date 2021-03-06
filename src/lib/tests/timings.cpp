#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <math.h>
#include <memory.h>
#include <tuple>
#include <vector>

#include "anglekernels.h"              // from mdtraj
#include "arrops.h"
#include "calc_distances.h"            // from mdanalysis
#include "distancekernels.h"           // from mdtraj
#include "distopia.h"                  // a fancy approach
#include "distopia_better_distances.h" // Jakub's fancy approach
#include "vanilla.h"                   // a naive approach

bool loadHeader(FILE *fp, int *Ncoords, float *box) {
  // header format:
  // natoms
  // boxx, boxy, boxz
  char tmp[1024];

  if (!fgets(tmp, 1024, fp))
    abort();

  *Ncoords = strtol(tmp, nullptr, 10);

  fgets(tmp, 1024, fp);
  char *next = tmp;
  for (unsigned i = 0; i < 3; ++i)
    *box++ = strtof(next, &next);

  return true;
}

bool loadCoords(FILE *fp, int Ncoords, float *coords) {
  char tmp[4096];

  for (unsigned int i = 0; i < Ncoords; ++i) {
    fgets(tmp, 4096, fp);
    char *next = tmp;
    for (unsigned char j = 0; j < 3; ++j)
      *coords++ = strtof(next, &next);
  }

  return true;
}

std::tuple<double, double>
timings(std::vector<std::chrono::duration<double>> tvec, int niter,
        int nresults, std::string name, std::ofstream &timings_f) {
  double time_sum = 0;
  double average_time = 0;
  double per_result = 0;
  for (size_t i = 0; i < tvec.size(); i++) {
    time_sum += tvec[i].count();
  }
  average_time = time_sum / niter;
  per_result = average_time / nresults;

  printf("\nDoing statistics for  %s \n", name.c_str());
  std::cout << "total time    " << time_sum << "  over number of iters "
            << niter << "\n";
  std::cout << "time average  " << average_time << "\n";
  std::cout << "per result    " << per_result << "\n";
  timings_f << name << "     " << per_result << "    " << time_sum << "\n";
  return std::make_tuple(per_result, time_sum);
}

// doesnt detect NAN, we should move to EXPECT_FLOAT_EQ() in gtest
#define TOL 0.0005 // we should pay attention to this tol
static bool verify(const float *ref, const float *other, unsigned int Ncoords) {
  for (unsigned int i = 0; i < Ncoords; ++i)
    if (fabs(ref[i] - other[i]) > TOL) {
      printf("wrong at pos %i\n", i);
      return false;
    }
  return true;
}

int main(int argc, char *argv[]) {
  // usage: file.in niters
  if (argc <= 2) {
    printf("Too few arguments, please supply a coordinate file and a number of "
           "iterations as a command "
           "line argument.\n");
    return (0);
  } else if (argc > 3) {
    printf("Too many arguments\n");
    return (0);
  }

  char *fname = argv[1];
  size_t niters = (size_t)std::stoi(argv[2]);

  float box[3];
  float *coords, *coords1, *coords2, *coords3;
  int Ncoords = 0;

  printf("\nBEGIN TIMINGS\n");
  printf("Number of iterations selected %i \n", niters);

  FILE *fp = fopen(fname, "r");
  if (!fp)
    return 1;
  if (!loadHeader(fp, &Ncoords, box))
    return 2;

  coords = (float *)malloc(Ncoords * 3 * sizeof(float));

  loadCoords(fp, Ncoords, coords);

  printf("Read %i coordinates \n", Ncoords);

  // DISTANCES
  // split coordinates in half
  if (Ncoords % 2 != 0) {
    printf("Ncoords is not divisible by 2 \n");
    return 1;
  }
  // if (Ncoords % 3 != 0) {
  //   printf("Ncoords is not divisible by 3 \n");
  //   return 1;
  // }

  std::cout << "\nDISTANCES\n";

  coords1 = coords;
  coords2 = coords + (3 * Ncoords / 2);
  int nresults_bonds = Ncoords / 2;
  float *results = (float *)malloc(nresults_bonds * sizeof(float));
  float *ref_results = (float *)malloc(nresults_bonds * sizeof(float));

  std::chrono::steady_clock::time_point t1, t2;
  std::chrono::duration<double> dt;

  std::vector<std::chrono::duration<double>> vanilla_calc_bonds;
  std::vector<std::chrono::duration<double>> mda_calc_bonds;
  std::vector<std::chrono::duration<double>> mdtraj_calc_bonds;
  std::vector<std::chrono::duration<double>> intrinsic_calc_bonds;
  std::vector<std::chrono::duration<double>> nint_calc_bonds;
  std::vector<std::chrono::duration<double>> fma_calc_bonds;
  std::vector<std::chrono::duration<double>> ymm_calc_bonds;

  for (size_t i = 0; i < niters; i++) {

    // Vanilla
    t1 = std::chrono::steady_clock::now();
    VanillaCalcBonds(coords1, coords2, box, nresults_bonds, results);
    t2 = std::chrono::steady_clock::now();
    dt = (t2 - t1);
    vanilla_calc_bonds.push_back(dt);

    memcpy(ref_results, results, sizeof(float) * nresults_bonds);

    // MDA
    t1 = std::chrono::steady_clock::now();
    _calc_bond_distance_ortho((coordinate *)coords1, (coordinate *)coords2,
                              nresults_bonds, box, results);
    t2 = std::chrono::steady_clock::now();
    dt = (t2 - t1);
    mda_calc_bonds.push_back(dt);
    if (!verify(ref_results, results, nresults_bonds))
      printf("MDA result wrong!\n");

    // MDTraj
    t1 = std::chrono::steady_clock::now();
    dist_mic(coords1, coords2, box, results, nresults_bonds);
    t2 = std::chrono::steady_clock::now();
    dt = (t2 - t1);
    mdtraj_calc_bonds.push_back(dt);
    if (!verify(ref_results, results, nresults_bonds))
      printf("MDtraj result wrong!\n");

    // hand rolled
    t1 = std::chrono::steady_clock::now();
    CalcBondsOrtho(coords1, coords2, box, nresults_bonds, results);
    t2 = std::chrono::steady_clock::now();
    dt = (t2 - t1);
    intrinsic_calc_bonds.push_back(dt);
    if (!verify(ref_results, results, nresults_bonds))
      printf("XMM result wrong!\n");

    // Nint based function
    t1 = std::chrono::steady_clock::now();
    CalcBondsNINT(coords1, coords2, box, nresults_bonds, results);
    t2 = std::chrono::steady_clock::now();
    dt = (t2 - t1);
    nint_calc_bonds.push_back(dt);
    if (!verify(ref_results, results, nresults_bonds))
      printf("NINT result wrong!\n");

    // FMA based function
    t1 = std::chrono::steady_clock::now();
    CalcBondsFMA(coords1, coords2, box, nresults_bonds, results);
    t2 = std::chrono::steady_clock::now();
    dt = (t2 - t1);
    fma_calc_bonds.push_back(dt);
    if (!verify(ref_results, results, nresults_bonds))
      printf("FMA result wrong!\n");

#if DISTOPIA_USE_AVX || DISTOPIA_USE_AVX2
    // YMM based function
    t1 = std::chrono::steady_clock::now();
    CalcBonds256(coords1, coords2, box, nresults_bonds, results);
    t2 = std::chrono::steady_clock::now();
    dt = (t2 - t1);
    ymm_calc_bonds.push_back(dt);
    if (!verify(ref_results, results, nresults_bonds))
      printf("YMM result wrong!\n");
#endif // #if DISTOPIA_USE_AVX || DISTOPIA_USE_AVX2 

    // ANGLES
    // split coordinates in three

    // std::cout << "\nANGLES\n";

    // if (Ncoords % 3 != 0) {
    //   std::cout << "Ncoords " << Ncoords
    //             << "are not able to be split into 3 \n";
    //   return 1;
    // }

    // coords1 = coords;
    // coords2 = coords + (3 * Ncoords / 3);
    // coords3 = coords + (6 * Ncoords / 3);
    // Nresults = Ncoords / 3;

    // // these are not strictly nessecary (should we check for failed realloc?)
    // results = (float *)realloc(results, Nresults * sizeof(float));
    // ref_results = (float *)realloc(ref_results, Nresults * sizeof(float));

    // t1 = std::chrono::steady_clock::now();
    // // seems sensitive to roundoff
    // VanillaCalcAngles(coords1, coords2, coords3, box, Nresults, results);

    // t2 = std::chrono::steady_clock::now();

    // dt = (t2 - t1);
    // // std::cout << "Regular calc_angles:    " << dt.count() << "\n";
    // // std::cout << "per result calc_angles: " << dt.count() / Nresults <<
    // "\n"; memcpy(ref_results, results, sizeof(float) * Nresults);

    // t1 = std::chrono::steady_clock::now();

    // _calc_angle_ortho((coordinate *)coords1, (coordinate *)coords2,
    //                   (coordinate *)coords3, Nresults, box, results);
    // t2 = std::chrono::steady_clock::now();

    // dt = (t2 - t1);
    // // std::cout << "MDA calc_angles:        " << dt.count() << "\n";
    // // std::cout << "per result MDA:         " << dt.count() / Nresults <<
    // "\n";

    // if (!verify(ref_results, results, Nresults)) {
    //   std::cout << "MDA result wrong!\n";
    // } else {
    //   std::cout << "MDA Results verified\n";
    // }

    // t1 = std::chrono::steady_clock::now();

    // angle_mic(coords1, coords2, coords3, box, results, Nresults);

    // t2 = std::chrono::steady_clock::now();

    // dt = (t2 - t1);

    // // std::cout << "MDtraj calc_angles:     " << dt.count() << "\n";
    // // std::cout << "per result MDtraj:      " << dt.count() / Nresults <<
    // "\n";

    // if (!verify(ref_results, results, Nresults)) {
    //   std::cout << "MDTraj result wrong!\n";
    // } else {
    //   std::cout << "MDTraj Results verified\n";
    // }

    // t1 = std::chrono::steady_clock::now();

    // CalcAnglesOrtho(coords1, coords2, coords3, box, Nresults, results);

    // t2 = std::chrono::steady_clock::now();

    // dt = (t2 - t1);

    // // std::cout << "XMM calc_angles:        " << dt.count() << "\n";
    // // std::cout << "per result XMM:         " << dt.count() / Nresults <<
    // "\n";

    // if (!verify(ref_results, results, Nresults)) {
    //   std::cout << "XMM result wrong!\n";
    // } else {
    //   std::cout << "XMM Results verified\n";
    // }
  }

  printf("STATISTICS\n");

  std::ofstream timings_f;
  timings_f.open("timings.dat");
  auto vanilla =
      timings(vanilla_calc_bonds, niters, nresults_bonds, "Vanilla", timings_f);
  auto mda = timings(mda_calc_bonds, niters, nresults_bonds, "MDA", timings_f);
  auto mdt =
      timings(mdtraj_calc_bonds, niters, nresults_bonds, "MDTraj", timings_f);
  auto xmm =
      timings(intrinsic_calc_bonds, niters, nresults_bonds, "XMM", timings_f);
  auto nint =
      timings(nint_calc_bonds, niters, nresults_bonds, "NINT", timings_f);
  auto fma = timings(fma_calc_bonds, niters, nresults_bonds, "FMA", timings_f);
#if DISTOPIA_USE_AVX || DISTOPIA_USE_AVX2
  auto ymm = timings(ymm_calc_bonds, niters, nresults_bonds, "YMM", timings_f);
#endif
  timings_f.close();

  printf("\nRELATIVE SPEEDUP\n\n");
  float mda_scaled = std::get<0>(vanilla) / std::get<0>(mda);
  printf("MDA speedup relative to vanilla    %f \n", mda_scaled);
  float mdt_scaled = std::get<0>(vanilla) / std::get<0>(mdt);
  printf("MDTraj speedup relative to vanilla %f \n", mdt_scaled);
  float xmm_scaled = std::get<0>(vanilla) / std::get<0>(xmm);
  printf("XMM speedup relative to vanilla    %f \n", xmm_scaled);
  float nint_scaled = std::get<0>(vanilla) / std::get<0>(nint);
  printf("Nint speedup relative to vanilla   %f \n", nint_scaled);
  float fma_scaled = std::get<0>(vanilla) / std::get<0>(fma);
  printf("FMA speedup relative to vanilla    %f \n", fma_scaled);
#if DISTOPIA_USE_AVX || DISTOPIA_USE_AVX2
  float ymm_scaled = std::get<0>(vanilla) / std::get<0>(ymm);
  printf("YMM speedup relative to vanilla    %f \n", ymm_scaled);
#endif

  return 0;
}

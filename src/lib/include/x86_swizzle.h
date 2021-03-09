#ifndef DISTOPIA_SWIZZLE_H
#define DISTOPIA_SWIZZLE_H

#include "arch_config.h"
#include "compiler_hints.h"
#include "distopia_type_traits.h"

#ifdef DISTOPIA_X86_SSE4_1

#include "x86_tgintrin.h"
#include <emmintrin.h>
#include <immintrin.h>

namespace {

// shuffle first element of __m128 to end and shunt everything left by one
inline __m128 ShuntFirst2Last(const __m128 input) {
  // PRE: input = abcd
  return shuffle_p<_MM_SHUFFLE(0, 3, 2, 1)>(input, input);
  // return bcda
}

#ifdef DISTOPIA_X86_AVX2_FMA

// shuffle first element of __m256d to end and shunt everything left by one
// NOTE requires AVX2 rather than AVX
// NOTE is it possible to do this with a lower latency by using shuffle and
// blend rather than the lane crossing instruction ?
inline __m256d ShuntFirst2Last(const __m256d input) {
  // PRE: input = abcd
  return _mm256_permute4x64_pd(input, _MM_SHUFFLE(0, 3, 2, 1));
  // return bcda
}

#endif // DISTOPIA_X86_AVX2_FMA

// safely loads values from an array of ScalarT to a SIMD type of VectorT with
// width of 4, checking that we dont read off the end of ScalarT.
template <typename VectorT>
inline VectorT SafeIdxLoad4(const VectorToScalarT<VectorT> *source,
                            const int idx,
                            const VectorToScalarT<VectorT> *end) {
  static_assert(ValuesPerPack<VectorT> == 4,
                "can only use to load into SIMD datatype of width 4");
  VectorT tmp;
  if (distopia_likely(source + idx + ValuesPerPack<VectorT> < end)) {
    // load as is, no overflow, likely path
    tmp = loadu_p<VectorT>(&source[idx]);
  } else {
    // load offset by one, unlikely path
    tmp = loadu_p<VectorT>(&source[idx - 1]);
    tmp = ShuntFirst2Last(tmp);
  }
  return tmp;
}

// transforms xyz coordinates from AOS to SOA
inline void Deinterleave4x3(const __m128 a, const __m128 b, const __m128 c,
                            const __m128 d, __m128 &x, __m128 &y, __m128 &z) {
  // U = undefined, X = junk
  // PRE: a  = x0y0z0X b = x1y1z1X c = x2y2z2X d = x3y3z3X
  __m128 tmp0 = _mm_unpacklo_ps(a, b);
  // tmp0 = x0x1y0y1
  __m128 tmp2 = _mm_unpacklo_ps(c, d);
  // tmp2 = x2x3y2y3
  __m128 tmp1 = _mm_unpackhi_ps(a, b);
  // tmp1 = z0z1XX
  __m128 tmp3 = _mm_unpackhi_ps(c, d);
  // tmp3 = z2z3XX
  x = _mm_movelh_ps(tmp0, tmp2);
  // x = x0x1x2x3
  y = _mm_movehl_ps(tmp2, tmp0);
  // y = y0y1y2y3
  z = _mm_movelh_ps(tmp1, tmp3);
  // z = z0z1z2z3
}

#ifdef DISTOPIA_X86_AVX

// transforms xyz coordinates from AOS to SOA
// NOTE can probably be improved
inline void Deinterleave4x3(const __m256d a, const __m256d b, const __m256d c,
                            const __m256d d, __m256d &x, __m256d &y,
                            __m256d &z) {
  // U = undefined, X = junk
  // PRE: a  = x0y0z0X b = x1y1z1X c = x2y2z2X d = x3y3z3X
  __m256d tmp0 = _mm256_unpacklo_pd(a, b);
  // tmp0 = x0x1z0z1
  __m256d tmp2 = _mm256_unpacklo_pd(c, d);
  // tmp2 = x2x3z2z3
  __m128d x_upper = _mm256_castpd256_pd128(tmp2);
  // x_upper = x2x3
  x = _mm256_insertf128_pd(tmp0, x_upper, 1);
  // x0x1x2x3
  __m128d z_lower = _mm256_extractf128_pd(tmp0, 1);
  // z_lower = z0z1
  __m128d z_upper = _mm256_extractf128_pd(tmp2, 1);
  z = _mm256_insertf128_pd(z, z_lower, 0);
  // z = z0z1UU
  z = _mm256_insertf128_pd(z, z_upper, 1);
  // z =  z0z1z2z3
  __m256d tmp1 = _mm256_unpackhi_pd(a, b);
  // tmp1 = y0y1XX
  __m256d tmp3 = _mm256_unpackhi_pd(c, d);
  // tmp3 = y2y3XX
  __m128d y_upper = _mm256_castpd256_pd128(tmp3);
  // y_upper = y2y3
  y = _mm256_insertf128_pd(tmp1, y_upper, 1);
  // y = y0y1y2y3
}

// transforms xyz coordinates from AOS to SOA
inline void Deinterleave8x3(const __m128 a, const __m128 b, const __m128 c,
                            const __m128 d, const __m128 e, const __m128 f,
                            const __m128 g, const __m128 h, __m256 &x,
                            __m256 &y, __m256 &z) {
  // U = undefined, X = junk
  // PRE: a  = x0y0z0X b = x1y1z1X c = x2y2z2X d = x3y3z3X e  = x4y4z4X f =
  // x5y5z5X g = x6y6z6X h = x7y7z7X
  __m128 tx0, ty0, tz0, tx1, ty1, tz1;
  Deinterleave4x3(a, b, c, d, tx0, ty0, tz0);
  // tx0 = x0x1x2x3 ty0 = y0y1y2y3 tz0 = z0z1z2z3
  Deinterleave4x3(e, f, g, h, tx1, ty1, tz1);
  // tx1 = x4x5x6x7 ty1 = y4y5y6y7 tz1 = z4z5z6z7

  // now combine
  x = _mm256_castps128_ps256(tx0);
  // x = x0x1x2x3UUUU
  x = _mm256_insertf128_ps(x, tx1, 1);
  // x = x0x1x2x3x4x5x6x7
  y = _mm256_castps128_ps256(ty0);
  // y = y0y1y2y3UUUU
  y = _mm256_insertf128_ps(y, ty1, 1);
  // y = y0y1y2y3y4y5y6y7
  z = _mm256_castps128_ps256(tz0);
  // z = z0z1z2z3UUUU
  z = _mm256_insertf128_ps(z, tz1, 1);
  // z = z0z1z2z3z4z5z6z7
}

#endif // DISTOPIA_X86_AVX

// wraps the individual deinterleaves, use of VectorToLoadT is required for
// __m256 case which takes an array of __m128, instead of the same type.


inline void DeinterleaveIdx(const __m128 *vec_arr, __m128 &x,
                                       __m128 &y, __m128 &z) {
  Deinterleave4x3(vec_arr[0], vec_arr[1], vec_arr[2], vec_arr[3], x, y, z);
}

#ifdef DISTOPIA_X86_AVX

inline void DeinterleaveIdx(const __m256d *vec_arr, __m256d &x,
                                        __m256d &y, __m256d &z) {
  Deinterleave4x3(vec_arr[0], vec_arr[1], vec_arr[2], vec_arr[3], x, y, z);
}

inline void DeinterleaveIdx(const __m128 *vec_arr, __m256 &x,
                                       __m256 &y, __m256 &z) {
  Deinterleave8x3(vec_arr[0], vec_arr[1], vec_arr[2], vec_arr[3], vec_arr[4],
                  vec_arr[5], vec_arr[6], vec_arr[7], x, y, z);
}

#endif // DISTOPIA_X86_AVX

inline void Deinterleave3(__m128 a, __m128 b, __m128 c, __m128 &x, __m128 &y,
                          __m128 &z) {
  // PRE: a = x0y0z0x1, b = y1z1x2y2, c = z2x3y3z3
  __m128 t1 = shuffle_p<_MM_SHUFFLE(2, 1, 3, 2)>(b, c);
  __m128 t2 = shuffle_p<_MM_SHUFFLE(1, 0, 2, 1)>(a, b);
  // t1 = x2y2x3y3, t2 = y0z0y1z1
  x = shuffle_p<_MM_SHUFFLE(2, 0, 3, 0)>(a, t1);
  y = shuffle_p<_MM_SHUFFLE(3, 1, 2, 0)>(t2, t1);
  z = shuffle_p<_MM_SHUFFLE(3, 0, 3, 1)>(t2, c);
  // x = x0x1x2x3, y = y0y1y2y3, z = z0z1z2z3
}
inline void Deinterleave3(__m128d a, __m128d b, __m128d c, __m128d &x,
                          __m128d &y, __m128d &z) {
  // PRE: a = x0y0, b = z0x1, c = y1z1
  x = blend_p<0x2>(a, b);
  y = shuffle_p<0x1>(a, c);
  z = blend_p<0x2>(b, c);
  // x = x0x1, y = y0y1, z = z0z1
}
#ifdef DISTOPIA_X86_AVX
inline void Deinterleave3(__m256 a, __m256 b, __m256 c, __m256 &x, __m256 &y,
                          __m256 &z) {
  // PRE: a = x0y0z0x1y1z1x2y2, b = z2x3y3z3x4y4z4x5, c = y6z6x7y7z7x8y8z8
  __m256 m1 = blend_p<0xf0>(a, b);
  __m256 m2 = permute2f128_p<0x21>(a, c);
  __m256 m3 = blend_p<0xf0>(b, c);
  // m1 = x0y0z0x1x4y4z4x5, m2 = y1z1x2y2y5z5x6y6, m3 = z2x3y3z3z6x7y7z7
  __m256 t1 = shuffle_p<_MM_SHUFFLE(2, 1, 3, 2)>(m2, m3);
  __m256 t2 = shuffle_p<_MM_SHUFFLE(1, 0, 2, 1)>(m1, m2);
  // t1 = x2y2x3y3x6y6x7y7, t2 = y0z0y1z1y4z4y5z5
  x = shuffle_p<_MM_SHUFFLE(2, 0, 3, 0)>(m1, t1);
  y = shuffle_p<_MM_SHUFFLE(3, 1, 2, 0)>(t2, t1);
  z = shuffle_p<_MM_SHUFFLE(3, 0, 3, 1)>(t2, m3);
  // x = x0x1x2x3x4x5x6x7, y = y0y1y2y3y4y5y6y7, z = z0z1z2z3z4z5z6z7
}
inline void Deinterleave3(__m256d a, __m256d b, __m256d c, __m256d &x,
                          __m256d &y, __m256d &z) {
  // a = x0y0z0x1, b = y1z1x2y2, c = z2x3y3z3
  __m256d m1 = blend_p<0xc>(a, b);
  __m256d m2 = permute2f128_p<0x21>(a, c);
  __m256d m3 = blend_p<0xc>(b, c);
  // m1 = x0y0x2y2, m2 = z0x1z2x3, m3 = y1z1y3z3
  x = blend_p<0xa>(m1, m2);
  y = shuffle_p<0x5>(m1, m3);
  z = blend_p<0xa>(m2, m3);
  // x = x0x1x2x3, y = y0y1y2y3, z = z0z1z2z3
}
#endif

inline void Interleave3(float x, float y, float z, __m128 &a, __m128 &b,
                        __m128 &c) {
  a = set_p(x, z, y, x);
  b = set_p(y, x, z, y);
  c = set_p(z, y, x, z);
  // a = xyzx, b = yzxy, c = zxyz
}
inline void Interleave3(double x, double y, double z, __m128d &a, __m128d &b,
                        __m128d &c) {
  a = set_p(y, x);
  b = set_p(x, z);
  c = set_p(z, y);
  // a = xy, b = zx, c = yz
}
#ifdef DISTOPIA_X86_AVX
inline void Interleave3(float x, float y, float z, __m256 &a, __m256 &b,
                        __m256 &c) {
  a = set_p(y, x, z, y, x, z, y, x);
  b = set_p(x, z, y, x, z, y, x, z);
  c = set_p(z, y, x, z, y, x, z, y);
  // a = xyzxyzxy, b = zxyzxyzx, c = yzxyzxyz
}
inline void Interleave3(double x, double y, double z, __m256d &a, __m256d &b,
                        __m256d &c) {
  a = set_p(x, z, y, x);
  b = set_p(y, x, z, y);
  c = set_p(z, y, x, z);
  // a = xyzx, b = yzxy, c = zxyz
}
#endif

} // namespace
#endif // DISTOPIA_X86_SSE4_1

#endif // DISTOPIA_SWIZZLE_H

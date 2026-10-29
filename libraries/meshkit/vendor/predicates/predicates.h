/*
 * predicates.h — C linkage for the subset of Shewchuk's robust geometric
 * predicates (vendor/predicates/predicates.c, public domain) that the mesh
 * generator uses. Call exactinit() once per process before any predicate.
 *
 * Provenance: predicates.c as mirrored by libigl-predicates (identical to
 * the original except that the SINGLE switch is spelled
 * LIBIGL_PREDICATES_USE_FLOAT and the sys/time include is commented out).
 */
#ifndef OPENSWMM_VENDOR_PREDICATES_H
#define OPENSWMM_VENDOR_PREDICATES_H

#ifdef __cplusplus
extern "C" {
#endif

void   exactinit(void);
/* Positive when pa, pb, pc are counter-clockwise; zero when collinear. */
double orient2d(const double *pa, const double *pb, const double *pc);
/* Positive when pd lies inside the circle through pa, pb, pc (given CCW). */
double incircle(const double *pa, const double *pb, const double *pc, const double *pd);

#ifdef __cplusplus
}
#endif

#endif

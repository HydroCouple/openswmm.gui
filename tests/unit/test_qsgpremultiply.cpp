/*!
 * \file   test_qsgpremultiply.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Headless tests for the shared premultiplied-alpha helper used by the QSG
 * map renderers (render/qsgpremultiply.h).
 */
#include "render/qsgpremultiply.h"

#include <gtest/gtest.h>

using OpenSWMM::Render::premul;

// Opaque is the identity: the premultiply sweep cannot change any
// currently-opaque rendering.
TEST(QsgPremultiply, OpaqueIsIdentity)
{
    for (int c = 0; c <= 255; ++c)
        EXPECT_EQ(premul(quint8(c), 255), quint8(c)) << "c=" << c;
}

// Fully transparent premultiplies to zero — the alpha-0 classification band
// regression case (straight alpha blended additively and painted a gray
// wash over the basemap).
TEST(QsgPremultiply, TransparentIsZero)
{
    for (int c = 0; c <= 255; ++c)
        EXPECT_EQ(premul(quint8(c), 0), quint8(0)) << "c=" << c;
}

// Exhaustive equivalence with the rounded real-valued definition
// round(c * a / 255).
TEST(QsgPremultiply, MatchesRoundedRealDefinition)
{
    for (int c = 0; c <= 255; ++c)
        for (int a = 0; a <= 255; ++a) {
            const int expected = (c * a + 127) / 255;
            EXPECT_EQ(premul(quint8(c), quint8(a)), quint8(expected))
                << "c=" << c << " a=" << a;
        }
}

// The integer form is symmetric in its arguments (used when composing a
// class alpha with a sublayer alpha).
TEST(QsgPremultiply, Commutative)
{
    for (int c = 0; c <= 255; c += 3)
        for (int a = 0; a <= 255; a += 3)
            EXPECT_EQ(premul(quint8(c), quint8(a)), premul(quint8(a), quint8(c)))
                << "c=" << c << " a=" << a;
}

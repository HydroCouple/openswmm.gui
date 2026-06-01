/*!
 * \file   test_meshbctype.cpp
 * \brief  Slice §V.VA — MeshBCTypes enum / INP token round-trip + engine
 *         BC type mapping.
 */

#include <gtest/gtest.h>

#include "mesh/meshbctype.h"

using mesh::MeshBCTypes;

TEST(MeshBCTypes, InpTokenRoundTrip)
{
    for (auto t : {MeshBCTypes::Type::Wall,
                   MeshBCTypes::Type::NormalFlow,
                   MeshBCTypes::Type::SpecifiedStageConst,
                   MeshBCTypes::Type::SpecifiedStageTS,
                   MeshBCTypes::Type::SpecifiedFlowConst,
                   MeshBCTypes::Type::SpecifiedFlowTS,
                   MeshBCTypes::Type::RatingCurve}) {
        bool ok = false;
        const QString tok = MeshBCTypes::inpToken(t);
        EXPECT_FALSE(tok.isEmpty());
        const MeshBCTypes::Type back = MeshBCTypes::fromInpToken(tok, &ok);
        EXPECT_TRUE(ok) << "Token '" << tok.toStdString() << "' did not parse back";
        EXPECT_EQ(back, t);
    }
}

TEST(MeshBCTypes, InpTokenIsCaseInsensitive)
{
    bool ok = false;
    EXPECT_EQ(MeshBCTypes::fromInpToken(QStringLiteral("normal_flow"), &ok),
              MeshBCTypes::Type::NormalFlow);
    EXPECT_TRUE(ok);
    ok = false;
    EXPECT_EQ(MeshBCTypes::fromInpToken(QStringLiteral("Ts_StAgE"), &ok),
              MeshBCTypes::Type::SpecifiedStageTS);
    EXPECT_TRUE(ok);
}

TEST(MeshBCTypes, FromInpTokenRejectsUnknown)
{
    bool ok = true;
    const auto t = MeshBCTypes::fromInpToken(QStringLiteral("MOAT"), &ok);
    EXPECT_FALSE(ok);
    EXPECT_EQ(t, MeshBCTypes::Type::Wall);  // safe default on failure
}

TEST(MeshBCTypes, EngineBCTypeCollapsesTSVariantsToBaseType)
{
    // Constant + TS variants of the same BC collapse onto the same
    // engine type — TS-ness is communicated via the tseries name slot,
    // not a separate type code. Matches the engine vocabulary at
    // openswmm_2d.h:325-327 + V-E4 (3) + V-E5 (4).
    EXPECT_EQ(MeshBCTypes::engineBCType(MeshBCTypes::Type::Wall),                0);
    EXPECT_EQ(MeshBCTypes::engineBCType(MeshBCTypes::Type::NormalFlow),          1);
    EXPECT_EQ(MeshBCTypes::engineBCType(MeshBCTypes::Type::SpecifiedStageConst), 2);
    EXPECT_EQ(MeshBCTypes::engineBCType(MeshBCTypes::Type::SpecifiedStageTS),    2);
    EXPECT_EQ(MeshBCTypes::engineBCType(MeshBCTypes::Type::SpecifiedFlowConst),  3);
    EXPECT_EQ(MeshBCTypes::engineBCType(MeshBCTypes::Type::SpecifiedFlowTS),     3);
    EXPECT_EQ(MeshBCTypes::engineBCType(MeshBCTypes::Type::RatingCurve),         4);
}

TEST(MeshBCTypes, LabelsAreNonEmpty)
{
    for (auto t : {MeshBCTypes::Type::Wall,
                   MeshBCTypes::Type::NormalFlow,
                   MeshBCTypes::Type::SpecifiedStageConst,
                   MeshBCTypes::Type::SpecifiedStageTS,
                   MeshBCTypes::Type::SpecifiedFlowConst,
                   MeshBCTypes::Type::SpecifiedFlowTS,
                   MeshBCTypes::Type::RatingCurve}) {
        EXPECT_FALSE(MeshBCTypes::label(t).isEmpty());
    }
}

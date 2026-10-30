#include "t.h"
#include <math.h>
#include "mathx.h"
#include "hullgeom.h"

static const float D2R = 0.01745329252f;
static const float FB = 0.45f, KEEL = -0.3f, TUCK = 0.45f, HW = 0.75f;

static void test_upright_cut_is_the_surface_height(void) {
    float y;
    float z = hull_side_z(HW, 1, FB, KEEL, TUCK, 0.0f, 0.1f, &y);
    T_NEAR(z, 0.1, 1e-4);
    T_ASSERT(y > 0 && y < HW);                       /* tucked in below the gunwale */
    z = hull_side_z(HW, -1, FB, KEEL, TUCK, 0.0f, 0.1f, &y);
    T_ASSERT(y < 0);
}

static void test_heeled_cut_lies_on_the_surface(void) {
    /* the cut point, once heeled, must sit exactly at the sea height */
    for (int deg = -80; deg <= 80; deg += 10) {
        float heel = deg * D2R;
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            float y, z = hull_side_z(HW, sgn, FB, KEEL, TUCK, heel, 0.0f, &y);
            if (z <= KEEL + 1e-4f || z >= FB - 1e-4f) continue;   /* clamped: no cut on this side */
            float world_z = z * cosf(heel) - y * sinf(heel);
            T_NEAR(world_z, 0.0, 1e-3);
        }
    }
}

static void test_heavy_heel_buries_leeward_rail_and_lifts_windward(void) {
    float y;
    float lee = hull_side_z(HW, 1, FB, KEEL, TUCK, 55 * D2R, 0.0f, &y);
    T_NEAR(lee, FB, 1e-4);                           /* starboard rail is under: nothing of that side shows */
    float wind = hull_side_z(HW, -1, FB, KEEL, TUCK, 55 * D2R, 0.0f, &y);
    T_ASSERT(wind < FB);                             /* the windward side is mostly clear of the water */
}

static void test_cut_is_symmetric_for_mirrored_heel(void) {
    float y1, y2;
    float z1 = hull_side_z(HW, 1, FB, KEEL, TUCK, 40 * D2R, 0.05f, &y1);
    float z2 = hull_side_z(HW, -1, FB, KEEL, TUCK, -40 * D2R, 0.05f, &y2);
    T_NEAR(z1, z2, 1e-4);
    T_NEAR(y1, -y2, 1e-4);
}

static void test_never_nan_whatever_the_heel(void) {
    for (int deg = -105; deg <= 105; deg++)
        for (int sgn = -1; sgn <= 1; sgn += 2)
            for (float surf = -0.5f; surf <= 0.7f; surf += 0.1f) {
                float y, z = hull_side_z(HW, sgn, FB, KEEL, TUCK, deg * D2R, surf, &y);
                T_ASSERT(isfinite(z) && isfinite(y) && z >= KEEL && z <= FB);
            }
}

static void test_hidden_side_flips_with_heel(void) {
    /* eye 4 m abeam to starboard and 3 m up: upright, the port side faces away */
    T_ASSERT(hull_hidden_side(0.0f, 4.0f, 3.0f, 1.5f) == -1);
    /* rolled far enough to starboard that the starboard side faces the water and the sky-facing side
     * (port) is toward the eye */
    T_ASSERT(hull_hidden_side(70 * D2R, 4.0f, 3.0f, 1.5f) == 1);
    T_ASSERT(hull_hidden_side(0.0f, 0.1f, 3.0f, 1.5f) == 0);   /* straight astern, both sides show */
}

static void test_deck_faces_eye_until_heeled_over(void) {
    /* trailing camera: well behind, a few metres up, on the centreline */
    T_ASSERT(hull_deck_faces_eye(0.0f, 0.0f, 3.0f));
    T_ASSERT(hull_deck_faces_eye(55 * D2R, 0.0f, 3.0f));      /* abaft the beam the deck still shows... */
    /* ...but with the eye off to the windward side and the boat heeled hard away, the deck turns its back */
    T_ASSERT(!hull_deck_faces_eye(55 * D2R, -4.0f, 3.0f));
    T_ASSERT(hull_deck_faces_eye(-55 * D2R, -4.0f, 3.0f));
}

static void test_clip_keeps_dry_part_and_marks_the_cut(void) {
    HgPt sq[4] = { { 0, -1, 1 }, { 0, 1, 1 }, { 0, 1, -1 }, { 0, -1, -1 } };   /* vertical panel across the sea */
    float d[4] = { 1, 1, -1, -1 };
    HgPt out[16]; bool cut[16];
    int m = hull_clip_above(sq, d, 4, out, cut, 16);
    T_ASSERT(m == 4);
    int ncut = 0;
    for (int i = 0; i < m; i++) {
        T_ASSERT(out[i].z >= -1e-5f);
        if (cut[i]) { ncut++; T_NEAR(out[i].z, 0.0, 1e-5); }
    }
    T_ASSERT(ncut == 2);
}

static void test_clip_all_wet_or_all_dry(void) {
    HgPt tri[3] = { { 0, 0, 1 }, { 1, 0, 1 }, { 0, 1, 1 } };
    float dry[3] = { 1, 1, 1 }, wet[3] = { -1, -1, -1 };
    HgPt out[8]; bool cut[8];
    T_ASSERT(hull_clip_above(tri, dry, 3, out, cut, 8) == 3);
    T_ASSERT(hull_clip_above(tri, wet, 3, out, cut, 8) == 0);
}

int main(void) {
    T_RUN(test_upright_cut_is_the_surface_height);
    T_RUN(test_heeled_cut_lies_on_the_surface);
    T_RUN(test_heavy_heel_buries_leeward_rail_and_lifts_windward);
    T_RUN(test_cut_is_symmetric_for_mirrored_heel);
    T_RUN(test_never_nan_whatever_the_heel);
    T_RUN(test_hidden_side_flips_with_heel);
    T_RUN(test_deck_faces_eye_until_heeled_over);
    T_RUN(test_clip_keeps_dry_part_and_marks_the_cut);
    T_RUN(test_clip_all_wet_or_all_dry);
    return T_DONE();
}

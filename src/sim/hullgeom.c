#include <math.h>
#include "hullgeom.h"

float hull_side_z(float w, int sgn, float fb, float keel, float tuck, float heel, float surf, float *y) {
    float ch = cosf(heel), sh = sinf(heel);
    float s = sgn * w;
    float a = (1.0f - tuck) / (fb - keel);        /* lateral tuck per metre of height */
    float z;
    float den = ch - s * sh * a;
    if (fabsf(den) < 1e-3f) {
        /* the side is parallel to the sea: all of it is under or none of it is */
        z = (fb * ch - s * sh < surf) ? fb : keel;
    } else {
        z = (surf + s * sh * (1.0f - a * fb)) / den;
        /* den < 0: the side leans over the water, so the cut runs the other way round */
        if (den < 0.0f) z = (fb * ch - s * sh < surf) ? fb : keel;
    }
    z = z < keel ? keel : z > fb ? fb : z;
    *y = s * (1.0f - (1.0f - tuck) * (fb - z) / (fb - keel));
    return z;
}

int hull_hidden_side(float heel, float lat_e, float h_e, float beam) {
    float t = cosf(heel) * lat_e - sinf(heel) * h_e;   /* eye offset along the starboard side's normal */
    if (fabsf(t) < 0.35f * beam) return 0;
    return t > 0.0f ? -1 : 1;
}

bool hull_deck_faces_eye(float heel, float lat_e, float h_e) {
    return sinf(heel) * lat_e + cosf(heel) * h_e > 0.0f;
}

int hull_clip_above(const HgPt *in, const float *d, int n, HgPt *out, bool *cut, int max) {
    int m = 0;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        bool ai = d[i] >= 0.0f, aj = d[j] >= 0.0f;
        if (ai && m < max) { out[m] = in[i]; cut[m] = false; m++; }
        if (ai != aj && m < max) {
            float t = d[i] / (d[i] - d[j]);
            out[m] = (HgPt){ in[i].f + (in[j].f - in[i].f) * t, in[i].l + (in[j].l - in[i].l) * t,
                             in[i].z + (in[j].z - in[i].z) * t };
            cut[m] = true; m++;
        }
    }
    return m;
}

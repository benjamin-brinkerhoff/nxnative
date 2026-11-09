// See nvn_bcdec.h. Decoding uses bcdec (dm/third_party/bcdec, single header, MIT / public domain).
#define BCDEC_IMPLEMENTATION
#define BCDEC_BC4BC5_PRECISE   // interface with signed variants of BC4 / BC5
#include "../../third_party/bcdec/bcdec.h"
#include "nvn_bcdec.h"
#include <stdlib.h>
#include <string.h>

enum {
    R8Unorm = 10, R8Snorm = 12,
    RG8Unorm = 30, RG8Snorm = 32,
    RGBA8Unorm = 70, RGBA8Unorm_sRGB = 71,
    RG11B10Float = 92,
    RGBA16Float = 115,
    ASTC_4x4_sRGB = 186, ASTC_4x4_LDR = 204
};

int nvn_bc_astc_on(void) {
    static int v = -1;
    if (v < 0) {
        const char *e = getenv("DM_BC_ASTC");
        v = !e || !*e || strcmp(e, "0") != 0;
    }
    return v;
}

// NVN: 0x41 RGB_DXT1, 0x42 RGBA_DXT1, 0x43 DXT3, 0x44 DXT5, 0x45-0x48 the same in sRGB,
// 0x49 / 0x4a BC4 unsigned / signed, 0x4b / 0x4c BC5, 0x4d / 0x4e BC7 (sRGB), 0x4f / 0x50 BC6H signed / unsigned
int nvn_bc_unpacked(uint32_t f, uint32_t *pf, int *bpp) {
    switch (f) {
    case 0x41: case 0x42: case 0x43: case 0x44: case 0x4d: *pf = RGBA8Unorm; *bpp = 4; return 1;
    case 0x45: case 0x46: case 0x47: case 0x48: case 0x4e: *pf = RGBA8Unorm_sRGB; *bpp = 4; return 1;
    case 0x49: *pf = R8Unorm; *bpp = 1; return 1;
    case 0x4a: *pf = R8Snorm; *bpp = 1; return 1;
    case 0x4b: *pf = RG8Unorm; *bpp = 2; return 1;
    case 0x4c: *pf = RG8Snorm; *bpp = 2; return 1;
    case 0x4f: *pf = RGBA16Float; *bpp = 8; return 1;
    case 0x50: *pf = RG11B10Float; *bpp = 4; return 1;
    default: return 0;
    }
}

int nvn_bc_target_fmt(uint32_t f, int target, uint32_t *pf, int *bw, int *bh, int *bpb) {
    if (target != 2 && nvn_bc_astc_on()) {
        switch (f) {
        case 0x41: case 0x42: case 0x43: case 0x44: case 0x4d:
            *pf = ASTC_4x4_LDR; *bw = 4; *bh = 4; *bpb = 16; return 1;
        case 0x45: case 0x46: case 0x47: case 0x48: case 0x4e:
            *pf = ASTC_4x4_sRGB; *bw = 4; *bh = 4; *bpb = 16; return 1;
        default: break;
        }
    }
    int bpp = 0;
    if (!nvn_bc_unpacked(f, pf, &bpp)) return 0;
    *bw = 1; *bh = 1; *bpb = bpp;
    return 1;
}

static inline uint32_t half_to_uf11(uint16_t h) {
    if (h & 0x8000u) return 0u;
    if (h >= 0x7c00u) return (h & 0x3ffu) ? 0u : 0x7bfu;
    uint32_t v = (uint32_t)h + 8u;
    return (v >= 0x7c00u) ? 0x7bfu : (v >> 4);
}

static inline uint32_t half_to_uf10(uint16_t h) {
    if (h & 0x8000u) return 0u;
    if (h >= 0x7c00u) return (h & 0x3ffu) ? 0u : 0x3dfu;
    uint32_t v = (uint32_t)h + 16u;
    return (v >= 0x7c00u) ? 0x3dfu : (v >> 5);
}

static inline uint64_t bitrev64(uint64_t x) {
    x = ((x >> 1) & 0x5555555555555555ULL) | ((x & 0x5555555555555555ULL) << 1);
    x = ((x >> 2) & 0x3333333333333333ULL) | ((x & 0x3333333333333333ULL) << 2);
    x = ((x >> 4) & 0x0F0F0F0F0F0F0F0FULL) | ((x & 0x0F0F0F0F0F0F0F0FULL) << 4);
    x = ((x >> 8) & 0x00FF00FF00FF00FFULL) | ((x & 0x00FF00FF00FF00FFULL) << 8);
    x = ((x >> 16) & 0x0000FFFF0000FFFFULL) | ((x & 0x0000FFFF0000FFFFULL) << 16);
    return (x >> 32) | (x << 32);
}

static const uint8_t k_q47_deq[48] = {
    0, 5, 11, 16, 21, 27, 32, 38, 43, 48, 54, 59, 65, 70, 76, 81, 86, 92, 97, 103, 108, 113, 119, 124,
    131, 136, 142, 147, 152, 158, 163, 169, 174, 179, 185, 190, 196, 201, 207, 212, 217, 223, 228, 234, 239, 244, 250, 255
};
static const uint8_t k_q47_ise[48] = {
    0, 16, 32, 2, 18, 34, 4, 20, 36, 6, 22, 38, 8, 24, 40, 10, 26, 42, 12, 28, 44, 14, 30, 46,
    47, 31, 15, 45, 29, 13, 43, 27, 11, 41, 25, 9, 39, 23, 7, 37, 21, 5, 35, 19, 3, 33, 17, 1
};
static const uint8_t k_trit_enc[243] = {
    0, 1, 2, 4, 5, 6, 8, 9, 10, 16, 17, 18, 20, 21, 22, 24, 25, 26, 3, 7, 11, 19, 23, 27, 12, 13, 14, 32, 33, 34, 36, 37, 38, 40, 41, 42, 48, 49, 50, 52, 53, 54, 56, 57, 58, 35, 39,
    43, 51, 55, 59, 44, 45, 46, 64, 65, 66, 68, 69, 70, 72, 73, 74, 80, 81, 82, 84, 85, 86, 88, 89, 90, 67, 71, 75, 83, 87, 91, 76, 77, 78, 128, 129, 130, 132, 133, 134, 136, 137, 138, 144, 145, 146, 148, 149, 150, 152, 153, 154,
    131, 135, 139, 147, 151, 155, 140, 141, 142, 160, 161, 162, 164, 165, 166, 168, 169, 170, 176, 177, 178, 180, 181, 182, 184, 185, 186, 163, 167, 171, 179, 183, 187, 172, 173, 174, 192, 193, 194, 196, 197, 198, 200, 201, 202,
    208, 209, 210, 212, 213, 214, 216, 217, 218, 195, 199, 203, 211, 215, 219, 204, 205, 206, 96, 97, 98, 100, 101, 102, 104, 105, 106, 112, 113, 114, 116, 117, 118, 120, 121, 122, 99, 103, 107, 115, 119, 123, 108, 109, 110, 224,
    225, 226, 228, 229, 230, 232, 233, 234, 240, 241, 242, 244, 245, 246, 248, 249, 250, 227, 231, 235, 243, 247, 251, 236, 237, 238, 28, 29, 30, 60, 61, 62, 92, 93, 94, 156, 157, 158, 188, 189, 190, 220, 221, 222, 31, 63, 95, 159,
    191, 223, 124, 125, 126
};

static inline uint32_t pack5t_er13(const uint8_t *v) {
    int tr = (v[0] >> 4) + (v[1] >> 4) * 3 + (v[2] >> 4) * 9 + (v[3] >> 4) * 27 + (v[4] >> 4) * 81;
    uint32_t T = k_trit_enc[tr];
    uint32_t b0 = v[0] & 15u, b1 = v[1] & 15u, b2 = v[2] & 15u, b3 = v[3] & 15u, b4 = v[4] & 15u;
    uint32_t lo10 = b0 | ((T & 3u) << 4) | (b1 << 6);
    uint32_t hi18 = ((T >> 2) & 3u) | (b2 << 2) | (((T >> 4) & 1u) << 6) | (b3 << 7) | (((T >> 5) & 3u) << 11) | (b4 << 13) | (((T >> 7) & 1u) << 17);
    return lo10 | (hi18 << 10);
}

static void rgba_to_astc4x4(const uint8_t px[64], uint8_t out[16]) {
    uint8_t rmin = 255, gmin = 255, bmin = 255, amin = 255;
    uint8_t rmax = 0, gmax = 0, bmax = 0, amax = 0;
    for (int i = 0; i < 16; i++) {
        uint8_t r = px[i*4+0], g = px[i*4+1], b = px[i*4+2], a = px[i*4+3];
        if (r < rmin) rmin = r;
        if (r > rmax) rmax = r;
        if (g < gmin) gmin = g;
        if (g > gmax) gmax = g;
        if (b < bmin) bmin = b;
        if (b > bmax) bmax = b;
        if (a < amin) amin = a;
        if (a > amax) amax = a;
    }
    if (rmin == rmax && gmin == gmax && bmin == bmax && amin == amax) {
        uint64_t lo = 0xFFFFFFFFFFFFFDFCULL;
        uint64_t hi = ((uint64_t)(rmin * 257U)) | ((uint64_t)(gmin * 257U) << 16) |
                      ((uint64_t)(bmin * 257U) << 32) | ((uint64_t)(amin * 257U) << 48);
        memcpy(out, &lo, 8);
        memcpy(out + 8, &hi, 8);
        return;
    }
    int sr = rmax - rmin, sg = gmax - gmin, sb = bmax - bmin;
    int ref_c = (sr >= sg && sr >= sb) ? 0 : (sg >= sb ? 1 : 2);
    int m_ref = (ref_c == 0) ? (rmin + rmax) : (ref_c == 1 ? (gmin + gmax) : (bmin + bmax));
    int mr = rmin + rmax, mg = gmin + gmax, mb = bmin + bmax, ma = amin + amax;
    int cov_r = 0, cov_g = 0, cov_b = 0, cov_a = 0;
    for (int i = 0; i < 16; i++) {
        int dref = (int)px[i*4 + ref_c] * 2 - m_ref;
        cov_r += dref * ((int)px[i*4+0] * 2 - mr);
        cov_g += dref * ((int)px[i*4+1] * 2 - mg);
        cov_b += dref * ((int)px[i*4+2] * 2 - mb);
        cov_a += dref * ((int)px[i*4+3] * 2 - ma);
    }
    int r0 = cov_r >= 0 ? rmin : rmax, r1 = cov_r >= 0 ? rmax : rmin;
    int g0 = cov_g >= 0 ? gmin : gmax, g1 = cov_g >= 0 ? gmax : gmin;
    int b0 = cov_b >= 0 ? bmin : bmax, b1 = cov_b >= 0 ? bmax : bmin;
    int a0 = cov_a >= 0 ? amin : amax, a1 = cov_a >= 0 ? amax : amin;
    if (r0 + g0 + b0 > r1 + g1 + b1) {
        int t;
        t = r0; r0 = r1; r1 = t;
        t = g0; g0 = g1; g1 = t;
        t = b0; b0 = b1; b1 = t;
        t = a0; a0 = a1; a1 = t;
    }
    if (amin == 255) {
        int dr = r1 - r0, dg = g1 - g0, db = b1 - b0;
        int len2 = dr * dr + dg * dg + db * db;
        uint64_t w48 = 0;
        if (len2 > 0) {
            for (int i = 0; i < 16; i++) {
                int dot = ((int)px[i*4+0] - r0) * dr + ((int)px[i*4+1] - g0) * dg + ((int)px[i*4+2] - b0) * db;
                int w = (dot * 7 + (len2 >> 1)) / len2;
                if (w < 0) w = 0; else if (w > 7) w = 7;
                w48 |= ((uint64_t)w) << (i * 3);
            }
        }
        uint64_t ep48 = (uint64_t)r0 | ((uint64_t)r1 << 8) | ((uint64_t)g0 << 16) |
                        ((uint64_t)g1 << 24) | ((uint64_t)b0 << 32) | ((uint64_t)b1 << 40);
        uint64_t lo = 0x10053ULL | (ep48 << 17);
        uint64_t hi = (ep48 >> 47) | bitrev64(w48);
        memcpy(out, &lo, 8);
        memcpy(out + 8, &hi, 8);
        return;
    }
    int dr = r1 - r0, dg = g1 - g0, db = b1 - b0, da = a1 - a0;
    int len2_rgba = dr * dr + dg * dg + db * db + da * da;
    static const int uw2[4] = {0, 21, 43, 64};
    uint64_t w32_m2 = 0;
    int err_m2 = 0;
    for (int i = 0; i < 16; i++) {
        int pr = px[i*4+0], pg = px[i*4+1], pb = px[i*4+2], pa = px[i*4+3];
        int w = 0;
        if (len2_rgba > 0) {
            int dot = (pr - r0) * dr + (pg - g0) * dg + (pb - b0) * db + (pa - a0) * da;
            w = (dot * 3 + (len2_rgba >> 1)) / len2_rgba;
            if (w < 0) w = 0; else if (w > 3) w = 3;
        }
        w32_m2 |= ((uint64_t)w) << (i * 2);
        int u = uw2[w];
        int er = pr - ((r0 * (64 - u) + r1 * u + 32) >> 6);
        int eg = pg - ((g0 * (64 - u) + g1 * u + 32) >> 6);
        int eb = pb - ((b0 * (64 - u) + b1 * u + 32) >> 6);
        int ea = pa - ((a0 * (64 - u) + a1 * u + 32) >> 6);
        err_m2 += er*er + eg*eg + eb*eb + ea*ea*2;
    }
    int qr0 = (r0 * 47 + 127) / 255, qr1 = (r1 * 47 + 127) / 255;
    int qg0 = (g0 * 47 + 127) / 255, qg1 = (g1 * 47 + 127) / 255;
    int qb0 = (b0 * 47 + 127) / 255, qb1 = (b1 * 47 + 127) / 255;
    int qa0 = (amin * 47 + 127) / 255, qa1 = (amax * 47 + 127) / 255;
    if (k_q47_deq[qr0] + k_q47_deq[qg0] + k_q47_deq[qb0] > k_q47_deq[qr1] + k_q47_deq[qg1] + k_q47_deq[qb1]) {
        int t;
        t = qr0; qr0 = qr1; qr1 = t;
        t = qg0; qg0 = qg1; qg1 = t;
        t = qb0; qb0 = qb1; qb1 = t;
    }
    int dr_q = k_q47_deq[qr1] - k_q47_deq[qr0], dg_q = k_q47_deq[qg1] - k_q47_deq[qg0], db_q = k_q47_deq[qb1] - k_q47_deq[qb0];
    int len2_q = dr_q * dr_q + dg_q * dg_q + db_q * db_q;
    int da_q = k_q47_deq[qa1] - k_q47_deq[qa0];
    uint64_t w64_dp = 0;
    int err_dp = 0;
    for (int i = 0; i < 16; i++) {
        int pr = px[i*4+0], pg = px[i*4+1], pb = px[i*4+2], pa = px[i*4+3];
        int wc = 0;
        if (len2_q > 0) {
            int dot = (pr - k_q47_deq[qr0]) * dr_q + (pg - k_q47_deq[qg0]) * dg_q + (pb - k_q47_deq[qb0]) * db_q;
            wc = (dot * 3 + (len2_q >> 1)) / len2_q;
            if (wc < 0) wc = 0; else if (wc > 3) wc = 3;
        }
        int wa = 0;
        if (da_q > 0) {
            wa = ((pa - k_q47_deq[qa0]) * 3 + (da_q >> 1)) / da_q;
            if (wa < 0) wa = 0; else if (wa > 3) wa = 3;
        }
        w64_dp |= ((uint64_t)(wc | (wa << 2))) << (i * 4);
        int uc = uw2[wc], ua = uw2[wa];
        int er = pr - ((k_q47_deq[qr0] * (64 - uc) + k_q47_deq[qr1] * uc + 32) >> 6);
        int eg = pg - ((k_q47_deq[qg0] * (64 - uc) + k_q47_deq[qg1] * uc + 32) >> 6);
        int eb = pb - ((k_q47_deq[qb0] * (64 - uc) + k_q47_deq[qb1] * uc + 32) >> 6);
        int ea = pa - ((k_q47_deq[qa0] * (64 - ua) + k_q47_deq[qa1] * ua + 32) >> 6);
        err_dp += er*er + eg*eg + eb*eb + ea*ea*2;
    }
    if (err_dp < err_m2) {
        uint8_t ev[10] = {
            k_q47_ise[qr0], k_q47_ise[qr1], k_q47_ise[qg0], k_q47_ise[qg1], k_q47_ise[qb0],
            k_q47_ise[qb1], k_q47_ise[qa0], k_q47_ise[qa1], 0, 0
        };
        uint64_t g0_28 = pack5t_er13(ev + 0);
        uint64_t g1_17 = pack5t_er13(ev + 5) & 0x1FFFFULL;
        uint64_t ep45 = g0_28 | (g1_17 << 28);
        uint64_t lo = 0x18442ULL | (ep45 << 17) | (3ULL << 62);
        uint64_t hi = bitrev64(w64_dp);
        memcpy(out, &lo, 8);
        memcpy(out + 8, &hi, 8);
    } else {
        uint64_t ep64 = (uint64_t)r0 | ((uint64_t)r1 << 8) | ((uint64_t)g0 << 16) | ((uint64_t)g1 << 24) |
                        ((uint64_t)b0 << 32) | ((uint64_t)b1 << 40) | ((uint64_t)a0 << 48) | ((uint64_t)a1 << 56);
        uint64_t lo = 0x18042ULL | (ep64 << 17);
        uint64_t hi = (ep64 >> 47) | bitrev64(w32_m2);
        memcpy(out, &lo, 8);
        memcpy(out + 8, &hi, 8);
    }
}

static void bc123_to_astc4x4(int fmt, const uint8_t *b, uint8_t out[16]) {
    // fmt: 0=BC1_RGB, 1=BC1_RGBA, 2=BC2, 3=BC3
    const uint8_t *cb = (fmt >= 2) ? (b + 8) : b;
    uint16_t c0 = cb[0] | ((uint16_t)cb[1] << 8);
    uint16_t c1 = cb[2] | ((uint16_t)cb[3] << 8);
    uint32_t ix = cb[4] | ((uint32_t)cb[5] << 8) | ((uint32_t)cb[6] << 16) | ((uint32_t)cb[7] << 24);

    int r0 = ((c0 >> 11) & 31) * 255 / 31;
    int g0 = ((c0 >> 5) & 63) * 255 / 63;
    int b0 = (c0 & 31) * 255 / 31;
    int r1 = ((c1 >> 11) & 31) * 255 / 31;
    int g1 = ((c1 >> 5) & 63) * 255 / 63;
    int b1 = (c1 & 31) * 255 / 31;

    int four_col = (fmt >= 2) || (c0 > c1);
    int has_trans_bc1 = 0;
    if (!four_col) {
        for (int i = 0; i < 16; i++) if (((ix >> (i * 2)) & 3) == 3) { has_trans_bc1 = 1; break; }
    }
    if (fmt <= 1 && has_trans_bc1) {
        uint8_t px[64];
        bcdec_bc1(b, px, 16);
        if (fmt == 0) for (int i = 0; i < 16; i++) px[i*4+3] = 255;
        rgba_to_astc4x4(px, out);
        return;
    }

    uint8_t a[16];
    uint8_t amin = 255, amax = 255;
    if (fmt == 2) {
        amin = 255; amax = 0;
        for (int i = 0; i < 16; i++) {
            uint8_t av = (uint8_t)(((b[i >> 1] >> ((i & 1) * 4)) & 15) * 17);
            a[i] = av;
            if (av < amin) amin = av;
            if (av > amax) amax = av;
        }
    } else if (fmt == 3) {
        uint8_t a0 = b[0], a1 = b[1];
        uint8_t ap[8];
        ap[0] = a0; ap[1] = a1;
        if (a0 > a1) {
            for (int k = 2; k < 8; k++) ap[k] = (uint8_t)(((8 - k) * a0 + (k - 1) * a1 + 3) / 7);
        } else {
            for (int k = 2; k < 6; k++) ap[k] = (uint8_t)(((6 - k) * a0 + (k - 1) * a1 + 2) / 5);
            ap[6] = 0; ap[7] = 255;
        }
        uint64_t alo = b[2] | ((uint64_t)b[3] << 8) | ((uint64_t)b[4] << 16) |
                       ((uint64_t)b[5] << 24) | ((uint64_t)b[6] << 32) | ((uint64_t)b[7] << 40);
        amin = 255; amax = 0;
        for (int i = 0; i < 16; i++) {
            uint8_t av = ap[(alo >> (i * 3)) & 7];
            a[i] = av;
            if (av < amin) amin = av;
            if (av > amax) amax = av;
        }
    }

    if (amin == 255) {
        int swap_ep = (r0 + g0 + b0 > r1 + g1 + b1);
        if (swap_ep) {
            int t;
            t = r0; r0 = r1; r1 = t;
            t = g0; g0 = g1; g1 = t;
            t = b0; b0 = b1; b1 = t;
        }
        if (four_col) {
            static const uint8_t bc_to_astc_w2[4] = {0, 3, 1, 2};
            uint64_t w32 = 0;
            for (int i = 0; i < 16; i++) {
                uint32_t w = bc_to_astc_w2[(ix >> (i * 2)) & 3] ^ (swap_ep ? 3u : 0u);
                w32 |= ((uint64_t)w) << (i * 2);
            }
            uint64_t ep48 = (uint64_t)r0 | ((uint64_t)r1 << 8) | ((uint64_t)g0 << 16) |
                            ((uint64_t)g1 << 24) | ((uint64_t)b0 << 32) | ((uint64_t)b1 << 40);
            uint64_t lo = 0x10042ULL | (ep48 << 17);
            uint64_t hi = (ep48 >> 47) | bitrev64(w32);
            memcpy(out, &lo, 8);
            memcpy(out + 8, &hi, 8);
        } else {
            static const uint8_t bc3_to_w3[4] = {0, 7, 3, 0};
            static const uint8_t bc3_to_w3_sw[4] = {7, 0, 4, 7};
            uint64_t w48 = 0;
            for (int i = 0; i < 16; i++) {
                uint32_t idx = (ix >> (i * 2)) & 3;
                uint32_t w = swap_ep ? bc3_to_w3_sw[idx] : bc3_to_w3[idx];
                w48 |= ((uint64_t)w) << (i * 3);
            }
            uint64_t ep48 = (uint64_t)r0 | ((uint64_t)r1 << 8) | ((uint64_t)g0 << 16) |
                            ((uint64_t)g1 << 24) | ((uint64_t)b0 << 32) | ((uint64_t)b1 << 40);
            uint64_t lo = 0x10053ULL | (ep48 << 17);
            uint64_t hi = (ep48 >> 47) | bitrev64(w48);
            memcpy(out, &lo, 8);
            memcpy(out + 8, &hi, 8);
        }
        return;
    }

    int qr0 = (r0 * 47 + 127) / 255, qr1 = (r1 * 47 + 127) / 255;
    int qg0 = (g0 * 47 + 127) / 255, qg1 = (g1 * 47 + 127) / 255;
    int qb0 = (b0 * 47 + 127) / 255, qb1 = (b1 * 47 + 127) / 255;
    int qa0 = (amin * 47 + 127) / 255, qa1 = (amax * 47 + 127) / 255;
    int swap_ep = (k_q47_deq[qr0] + k_q47_deq[qg0] + k_q47_deq[qb0] > k_q47_deq[qr1] + k_q47_deq[qg1] + k_q47_deq[qb1]);
    if (swap_ep) {
        int t;
        t = qr0; qr0 = qr1; qr1 = t;
        t = qg0; qg0 = qg1; qg1 = t;
        t = qb0; qb0 = qb1; qb1 = t;
    }

    static const uint8_t bc_to_astc_w2[4] = {0, 3, 1, 2};
    int da = amax - amin;
    uint64_t w64_dp = 0;
    for (int i = 0; i < 16; i++) {
        uint32_t idx = (ix >> (i * 2)) & 3;
        uint32_t wc = bc_to_astc_w2[idx] ^ (swap_ep ? 3u : 0u);
        uint32_t wa = (da > 0) ? (uint32_t)(((int)(a[i] - amin) * 3 + (da >> 1)) / da) : 0u;
        w64_dp |= ((uint64_t)(wc | (wa << 2))) << (i * 4);
    }
    uint8_t ev[10] = {
        k_q47_ise[qr0], k_q47_ise[qr1], k_q47_ise[qg0], k_q47_ise[qg1], k_q47_ise[qb0],
        k_q47_ise[qb1], k_q47_ise[qa0], k_q47_ise[qa1], 0, 0
    };
    uint64_t g0_28 = pack5t_er13(ev + 0);
    uint64_t g1_17 = pack5t_er13(ev + 5) & 0x1FFFFULL;
    uint64_t ep45 = g0_28 | (g1_17 << 28);
    uint64_t lo = 0x18442ULL | (ep45 << 17) | (3ULL << 62);
    uint64_t hi = bitrev64(w64_dp);
    memcpy(out, &lo, 8);
    memcpy(out + 8, &hi, 8);
}

static void block(uint32_t f, const uint8_t *b, uint8_t *o, int op) {   // one block -> 4x4 pixels (o has op bytes per row)
    switch (f) {
    case 0x41: case 0x42: case 0x45: case 0x46:
        bcdec_bc1(b, o, op);
        if (f == 0x41 || f == 0x45) {
            for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) o[y * op + x * 4 + 3] = 255;
        }
        break;
    case 0x43: case 0x47: bcdec_bc2(b, o, op); break;
    case 0x44: case 0x48: bcdec_bc3(b, o, op); break;
    case 0x49: bcdec_bc4(b, o, op, 0); break;
    case 0x4a: bcdec_bc4(b, o, op, 1); break;
    case 0x4b: bcdec_bc5(b, o, op, 0); break;
    case 0x4c: bcdec_bc5(b, o, op, 1); break;
    case 0x4d: case 0x4e: bcdec_bc7(b, o, op); break;
    case 0x4f: {   // signed BC6H -> RGBA16Float
        uint16_t rgb[16 * 3]; bcdec_bc6h_half(b, rgb, 4 * 3, 1);
        for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
            uint16_t *d = (uint16_t *)(o + y * op) + x * 4; const uint16_t *s = rgb + (y * 4 + x) * 3;
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 0x3c00;
        }
        break; }
    case 0x50: {   // unsigned BC6H -> RG11B10Float (4 bytes/pixel, 50% less memory than RGBA16Float)
        uint16_t rgb[16 * 3]; bcdec_bc6h_half(b, rgb, 4 * 3, 0);
        for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
            uint32_t *d = (uint32_t *)(o + y * op) + x; const uint16_t *s = rgb + (y * 4 + x) * 3;
            *d = half_to_uf11(s[0]) | (half_to_uf11(s[1]) << 11) | (half_to_uf10(s[2]) << 22);
        }
        break; }
    }
}

int nvn_bc_decode(uint32_t f, const uint8_t *src, size_t pitch, uint32_t w, uint32_t h, uint8_t *dst, size_t dpitch) {
    uint32_t pf; int bpp; if (!nvn_bc_unpacked(f, &pf, &bpp)) return -1;
    int bb = (f == 0x41 || f == 0x42 || f == 0x45 || f == 0x46 || f == 0x49 || f == 0x4a) ? 8 : 16;   // bytes per block
    uint8_t tmp[4 * 4 * 8];
    for (uint32_t by = 0; by * 4 < h; by++) for (uint32_t bx = 0; bx * 4 < w; bx++) {
        const uint8_t *b = src + by * pitch + bx * bb;
        uint32_t cw = w - bx * 4 < 4 ? w - bx * 4 : 4, ch = h - by * 4 < 4 ? h - by * 4 : 4;
        uint8_t *o = dst + (size_t)by * 4 * dpitch + (size_t)bx * 4 * bpp;
        if (cw == 4 && ch == 4) { block(f, b, o, (int)dpitch); continue; }
        block(f, b, tmp, 4 * bpp);
        for (uint32_t y = 0; y < ch; y++) memcpy(o + y * dpitch, tmp + y * 4 * bpp, cw * bpp);
    }
    return 0;
}

int nvn_bc_decode_target(uint32_t f, int target, const uint8_t *src, size_t pitch, uint32_t w, uint32_t h, uint8_t *dst, size_t dpitch) {
    uint32_t pf = 0; int bw = 1, bh = 1, bpb = 0;
    if (!nvn_bc_target_fmt(f, target, &pf, &bw, &bh, &bpb)) return -1;
    if (bw == 4 && bh == 4) {
        int bb = (f == 0x41 || f == 0x42 || f == 0x45 || f == 0x46) ? 8 : 16;
        uint32_t nbx = (w + 3u) / 4u, nby = (h + 3u) / 4u;
        int bc_kind = (f == 0x41 || f == 0x45) ? 0 : (f == 0x42 || f == 0x46) ? 1 : (f == 0x43 || f == 0x47) ? 2 : (f == 0x44 || f == 0x48) ? 3 : 4;
        uint8_t px[64];
        for (uint32_t by = 0; by < nby; by++) {
            const uint8_t *srow = src + (size_t)by * pitch;
            uint8_t *drow = dst + (size_t)by * dpitch;
            for (uint32_t bx = 0; bx < nbx; bx++) {
                const uint8_t *b = srow + (size_t)bx * bb;
                uint8_t *out = drow + (size_t)bx * 16;
                if (bc_kind < 4) {
                    bc123_to_astc4x4(bc_kind, b, out);
                } else {
                    bcdec_bc7(b, px, 16);
                    rgba_to_astc4x4(px, out);
                }
            }
        }
        return 0;
    }
    return nvn_bc_decode(f, src, pitch, w, h, dst, dpitch);
}

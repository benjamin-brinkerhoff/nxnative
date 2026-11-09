// GPUs without BC compressed format support (A15 / A16 etc., Apple GPU family Apple8 and below):
// 2D / array / cube BC1, BC2, BC3 and BC7 textures are transcoded to ASTC 4x4 (16 bytes per 4x4 block = 1 byte/px,
// 4x smaller than RGBA8), BC6H_U is unpacked to RG11B10Float (4 bytes/px, 2x smaller than RGBA16Float),
// BC4/BC5 are unpacked to R8/RG8, and 3D BC textures (or when DM_BC_ASTC=0) are unpacked to RGBA8.
#pragma once
#include <stddef.h>
#include <stdint.h>

// Whether BC1-3/BC7 -> ASTC 4x4 transcoding is enabled (default 1; set DM_BC_ASTC=0 to unpack to RGBA8 instead)
int nvn_bc_astc_on(void);

// NVN format is BC -> returns 1, *mtl_pf = target Metal pixel format for the given NVN texture target
// (target == 2 is 3D, where Metal disallows 2D ASTC formats), *bw/*bh = block dimensions (4x4 for ASTC, 1x1 for unpacked),
// *bpb = bytes per block/pixel in *mtl_pf. Returns 0 if nvn_fmt is not a BC format.
int nvn_bc_target_fmt(uint32_t nvn_fmt, int target, uint32_t *mtl_pf, int *bw, int *bh, int *bpb);

// Uncompressed fallback format (used for 3D textures, GL/VK helpers, or when DM_BC_ASTC=0):
// returns 1 if nvn_fmt is BC, setting *mtl_pf and *bpp (bytes per pixel); returns 0 otherwise.
int nvn_bc_unpacked(uint32_t nvn_fmt, uint32_t *mtl_pf, int *bpp);

// Decode a w x h pixel region into uncompressed format (nvn_bc_unpacked):
// src has pitch bytes per 4x4 block row; dst has dpitch bytes per pixel row. Returns 0 on success.
int nvn_bc_decode(uint32_t nvn_fmt, const uint8_t *src, size_t pitch, uint32_t w, uint32_t h, uint8_t *dst, size_t dpitch);

// Decode or transcode a w x h region into the format selected by nvn_bc_target_fmt(nvn_fmt, target, ...):
// src has pitch bytes per 4x4 BC block row; dst has dpitch bytes per target row (block row for ASTC, pixel row for unpacked).
int nvn_bc_decode_target(uint32_t nvn_fmt, int target, const uint8_t *src, size_t pitch, uint32_t w, uint32_t h, uint8_t *dst, size_t dpitch);

/*
 * Copyright (C) 2026 Rockchip Electronics Co., Ltd.
 * Authors:
 *  Cerf Yu <cerf.yu@rock-chips.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifdef LOG_TAG
#undef LOG_TAG
#define LOG_TAG "im2d_rga_csc"
#else
#define LOG_TAG "im2d_rga_csc"
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "rga.h"
#include "rga_ioctl.h"
#include "im2d_log.h"
#include "im2d_type.h"
#include "im2d_csc.h"
#include "im2d_debugger.h"

#include "core/utils/utils.h"

struct rga_csc_coef {
    int32_t coef00;
    int32_t coef01;
    int32_t coef02;

    int32_t coef10;
    int32_t coef11;
    int32_t coef12;

    int32_t coef20;
    int32_t coef21;
    int32_t coef22;
};

struct rga_csc_vector {
    int32_t offset0;
    int32_t offset1;
    int32_t offset2;
};

struct rga_csc_dc_coef {
    int32_t in_dc0;
    int32_t in_dc1;
    int32_t in_dc2;

    int32_t out_dc0;
    int32_t out_dc1;
    int32_t out_dc2;
};

struct rga_csc_mode {
    uint32_t input;
    uint32_t output;
};

struct rga_csc_coef_version {
    uint32_t pixel_depth;
    uint32_t coef_precision;
    const struct rga_csc_coef *csc_coefs;
};

static const struct rga_csc_mode g_csc_mode_table[] = {
    { IM_RGB_LIMIT_RANGE, IM_RGB_FULL_RANGE },                      // RGBL_TO_RGBF
    { IM_RGB_LIMIT_RANGE, IM_YUV_BT601_LIMIT_RANGE },               // RGBL_TO_YUV601L
    { IM_RGB_LIMIT_RANGE, IM_YUV_BT601_FULL_RANGE },                // RGBL_TO_YUV601F
    { IM_RGB_LIMIT_RANGE, IM_YUV_BT709_LIMIT_RANGE },               // RGBL_TO_YUV709L
    { IM_RGB_LIMIT_RANGE, IM_YUV_BT709_FULL_RANGE },                // RGBL_TO_YUV709F
    { IM_RGB_BT2020_LIMIT_RANGE, IM_YUV_BT2020_LIMIT_RANGE },       // RGB2020L_TO_YUV2020L
    { IM_RGB_BT2020_LIMIT_RANGE, IM_YUV_BT2020_FULL_RANGE },        // RGB2020L_TO_YUV2020F

    { IM_RGB_FULL_RANGE, IM_RGB_LIMIT_RANGE },                      // RGBF_TO_RGBL
    { IM_RGB_FULL_RANGE, IM_YUV_BT601_LIMIT_RANGE },                // RGBF_TO_YUV601L
    { IM_RGB_FULL_RANGE, IM_YUV_BT601_FULL_RANGE },                 // RGBF_TO_YUV601F
    { IM_RGB_FULL_RANGE, IM_YUV_BT709_LIMIT_RANGE },                // RGBF_TO_YUV709L
    { IM_RGB_FULL_RANGE, IM_YUV_BT709_FULL_RANGE },                 // RGBF_TO_YUV709F
    { IM_RGB_BT2020_FULL_RANGE, IM_YUV_BT2020_LIMIT_RANGE },        // RGB2020F_TO_YUV2020L
    { IM_RGB_BT2020_FULL_RANGE, IM_YUV_BT2020_FULL_RANGE },         // RGB2020F_TO_YUV2020F

    { IM_YUV_BT601_LIMIT_RANGE, IM_RGB_LIMIT_RANGE },               // YUV601L_TO_RGBL
    { IM_YUV_BT601_LIMIT_RANGE, IM_RGB_FULL_RANGE },                // YUV601L_TO_RGBF
    { IM_YUV_BT601_LIMIT_RANGE, IM_YUV_BT601_FULL_RANGE },          // YUV601L_TO_YUV601F
    { IM_YUV_BT601_LIMIT_RANGE, IM_YUV_BT709_LIMIT_RANGE },         // YUV601L_TO_YUV709L
    { IM_YUV_BT601_LIMIT_RANGE, IM_YUV_BT709_FULL_RANGE },          // YUV601L_TO_YUV709F

    { IM_YUV_BT601_FULL_RANGE, IM_RGB_LIMIT_RANGE },                // YUV601F_TO_RGBL
    { IM_YUV_BT601_FULL_RANGE, IM_RGB_FULL_RANGE },                 // YUV601F_TO_RGBF
    { IM_YUV_BT601_FULL_RANGE, IM_YUV_BT601_LIMIT_RANGE },          // YUV601F_TO_YUV601L
    { IM_YUV_BT601_FULL_RANGE, IM_YUV_BT709_LIMIT_RANGE },          // YUV601F_TO_YUV709L
    { IM_YUV_BT601_FULL_RANGE, IM_YUV_BT709_FULL_RANGE },           // YUV601F_TO_YUV709F

    { IM_YUV_BT709_LIMIT_RANGE, IM_RGB_LIMIT_RANGE },               // YUV709L_TO_RGBL
    { IM_YUV_BT709_LIMIT_RANGE, IM_RGB_FULL_RANGE },                // YUV709L_TO_RGBF
    { IM_YUV_BT709_LIMIT_RANGE, IM_YUV_BT601_LIMIT_RANGE },         // YUV709L_TO_YUV601L
    { IM_YUV_BT709_LIMIT_RANGE, IM_YUV_BT601_FULL_RANGE },          // YUV709L_TO_YUV601F
    { IM_YUV_BT709_LIMIT_RANGE, IM_YUV_BT709_FULL_RANGE },          // YUV709L_TO_YUV709F

    { IM_YUV_BT709_FULL_RANGE, IM_RGB_LIMIT_RANGE },                // YUV709F_TO_RGBL
    { IM_YUV_BT709_FULL_RANGE, IM_RGB_FULL_RANGE },                 // YUV709F_TO_RGBF
    { IM_YUV_BT709_FULL_RANGE, IM_YUV_BT601_LIMIT_RANGE },          // YUV709F_TO_YUV601L
    { IM_YUV_BT709_FULL_RANGE, IM_YUV_BT601_FULL_RANGE },           // YUV709F_TO_YUV601F
    { IM_YUV_BT709_FULL_RANGE, IM_YUV_BT709_LIMIT_RANGE },          // YUV709F_TO_YUV709L

    { IM_YUV_BT2020_LIMIT_RANGE, IM_RGB_BT2020_LIMIT_RANGE },       // YUV2020L_TO_RGB2020L
    { IM_YUV_BT2020_LIMIT_RANGE, IM_RGB_BT2020_FULL_RANGE },        // YUV2020L_TO_RGB2020F
    { IM_YUV_BT2020_LIMIT_RANGE, IM_YUV_BT2020_FULL_RANGE },        // YUV2020L_TO_YUV2020F

    { IM_YUV_BT2020_FULL_RANGE, IM_RGB_BT2020_LIMIT_RANGE },        // YUV2020F_TO_RGB2020L
    { IM_YUV_BT2020_FULL_RANGE, IM_RGB_BT2020_FULL_RANGE },         // YUV2020F_TO_RGB2020F
    { IM_YUV_BT2020_FULL_RANGE, IM_YUV_BT2020_LIMIT_RANGE },        // YUV2020F_TO_YUV2020L

    { IM_RGB_BT2020_FULL_RANGE, IM_RGB_BT2020_LIMIT_RANGE},         // RGB2020F_TO_RGB2020L
    { IM_RGB_BT2020_LIMIT_RANGE, IM_RGB_BT2020_FULL_RANGE},         // RGB2020L_TO_RGB2020F

    // { IM_COLOR_SPACE_DEFAULT, IM_COLOR_SPACE_DEFAULT},           // IDENTITY_MODE
};

/* for 8bit pixel depth + 8bit coef precision case */
static const struct rga_csc_coef g_csc_coefs_8bit_pix_8bit_precision[] = {
	{ 298,   0,   0,   0,  298,    0,   0,    0, 298 }, /* RGBL_TO_RGBF */
	{  77, 150,  29, -44,  -87,  131, 131, -110, -21 }, /* RGBL_TO_YUV601L */
	{  89, 175,  34, -50,  -99,  149, 149, -125, -24 }, /* RGBL_TO_YUV601F */
	{  54, 183,  19, -30, -101,  131, 131, -119, -12 }, /* RGBL_TO_YUV709L */
	{  63, 213,  22, -34, -115,  149, 149, -135, -14 }, /* RGBL_TO_YUV709F */
	{  67, 174,  15, -37,  -94,  131, 131, -120, -11 }, /* RGB2020L_TO_YUV2020L */
	{  78, 202,  18, -42, -107,  149, 149, -137, -12 }, /* RGB2020L_TO_YUV2020F */
	{ 220,   0,   0,   0,  220,    0,   0,    0, 220 }, /* RGBF_TO_RGBL */
	{  66, 129,  25, -38,  -74,  112, 112,  -94, -18 }, /* RGBF_TO_YUV601L */
	{  77, 150,  29, -43,  -85,  128, 128, -107, -21 }, /* RGBF_TO_YUV601F */
	{  47, 157,  16, -26,  -87,  113, 112, -102, -10 }, /* RGBF_TO_YUV709L */
	{  54, 183,  19, -29,  -99,  128, 128, -116, -12 }, /* RGBF_TO_YUV709F */
	{  58, 149,  13, -31,  -81,  112, 112, -103,  -9 }, /* RGB2020F_TO_YUV2020L */
	{  67, 174,  15, -36,  -92,  128, 128, -118, -10 }, /* RGB2020F_TO_YUV2020F */
	{ 256,   0, 351, 256,  -86, -179, 256,  444,   0 }, /* YUV601L_TO_RGBL */
	{ 298,   0, 409, 298, -100, -208, 298,  516,   0 }, /* YUV601L_TO_RGBF */
	{ 298,   0,   0,   0,  291,    0,   0,    0, 291 }, /* YUV601L_TO_YUV601F */
	{ 256, -30, -53,   0,  261,   29,   0,   19, 262 }, /* YUV601L_TO_YUV709L */
	{ 298, -34, -62,   0,  297,   33,   0,   22, 299 }, /* YUV601L_TO_YUV709F */
	{ 220,   0, 308, 220,  -76, -157, 220,  390,   0 }, /* YUV601F_TO_RGBL */
	{ 256,   0, 359, 256,  -88, -183, 256,  454,   0 }, /* YUV601F_TO_RGBF */
	{ 220,   0,   0,   0,  225,    0,   0,    0, 225 }, /* YUV601F_TO_YUV601L */
	{ 220, -26, -47,   0,  229,   26,   0,   17, 231 }, /* YUV601F_TO_YUV709L */
	{ 256, -30, -54,   0,  261,   29,   0,   19, 262 }, /* YUV601F_TO_YUV709F */
	{ 256,   0, 394, 256,  -47, -117, 256,  464,   0 }, /* YUV709L_TO_RGBL */
	{ 298,   0, 459, 298,  -55, -136, 298,  541,   0 }, /* YUV709L_TO_RGBF */
	{ 256,  25,  49,   0,  253,  -28,   0,  -19, 252 }, /* YUV709L_TO_YUV601L */
	{ 298,  30,  57,   0,  288,  -32,   0,  -21, 287 }, /* YUV709L_TO_YUV601F */
	{ 298,   0,   0,   0,  291,    0,   0,    0, 291 }, /* YUV709L_TO_YUV709F */
	{ 220,   0, 346, 220,  -41, -103, 220,  408,   0 }, /* YUV709F_TO_RGBL */
	{ 256,   0, 403, 256,  -48, -120, 256,  475,   0 }, /* YUV709F_TO_RGBF */
	{ 220,  22,  43,   0,  223,  -25,   0,  -16, 221 }, /* YUV709F_TO_YUV601L */
	{ 256,  26,  50,   0,  253,  -28,   0,  -19, 252 }, /* YUV709F_TO_YUV601F */
	{ 220,   0,   0,   0,  225,    0,   0,    0, 225 }, /* YUV709F_TO_YUV709L */
	{ 256,   0, 369, 256,  -41, -143, 256,  471,   0 }, /* YUV2020L_TO_RGB2020L */
	{ 298,   0, 430, 298,  -48, -167, 298,  548,   0 }, /* YUV2020L_TO_RGB2020F */
	{ 298,   0,   0,   0,  291,    0,   0,    0, 291 }, /* YUV2020L_TO_YUV2020F */
	{ 220,   0, 324, 220,  -36, -126, 220,  414,   0 }, /* YUV2020F_TO_RGB2020L */
	{ 256,   0, 377, 256,  -42, -146, 256,  482,   0 }, /* YUV2020F_TO_RGB2020F */
	{ 220,   0,   0,   0,  225,    0,   0,    0, 225 }, /* YUV2020F_TO_YUV2020L */
	{ 220,   0,   0,   0,  220,    0,   0,    0, 220 }, /* RGB2020F_TO_RGB2020L */
	{ 298,   0,   0,   0,  298,    0,   0,    0, 298 }, /* RGB2020L_TO_RGB2020F */
	// { 256,   0,   0,   0,  256,    0,   0,    0, 256 }, /* IDENTITY_MODE */
};

/* for 8bit pixel depth + 10bit coef precision case */
static const struct rga_csc_coef g_csc_coefs_8bit_pix_10bit_precision[] = {
	{ 1196,    0,    0,    0, 1196,    0,    0,    0, 1196 }, /* RGBL_TO_RGBF */
	{  306,  601,  117, -177, -347,  524,  524, -439,  -85 }, /* RGBL_TO_YUV601L */
	{  356,  700,  136, -202, -395,  596,  596, -499,  -97 }, /* RGBL_TO_YUV601F */
	{  218,  732,   74, -120, -404,  524,  524, -476,  -48 }, /* RGBL_TO_YUV709L */
	{  253,  853,   86, -137, -459,  596,  596, -541,  -55 }, /* RGBL_TO_YUV709F */
	{  269,  694,   61, -146, -378,  524,  524, -482,  -42 }, /* RGB2020L_TO_YUV2020L */
	{  313,  808,   71, -166, -430,  596,  596, -548,  -48 }, /* RGB2020L_TO_YUV2020F */
	{  879,    0,    0,    0,  879,    0,    0,    0,  879 }, /* RGBF_TO_RGBL */
	{  263,  516,  100, -152, -298,  450,  450, -377,  -73 }, /* RGBF_TO_YUV601L */
	{  306,  601,  117, -173, -339,  512,  512, -429,  -83 }, /* RGBF_TO_YUV601F */
	{  187,  629,   63, -103, -347,  450,  450, -409,  -41 }, /* RGBF_TO_YUV709L */
	{  218,  732,   74, -117, -395,  512,  512, -465,  -47 }, /* RGBF_TO_YUV709F */
	{  231,  596,   52, -126, -324,  450,  450, -414,  -36 }, /* RGB2020F_TO_YUV2020L */
	{  269,  694,   61, -143, -369,  512,  512, -471,  -41 }, /* RGB2020F_TO_YUV2020F */
	{ 1024,    0, 1404, 1024, -345, -715, 1024, 1774,    0 }, /* YUV601L_TO_RGBL */
	{ 1192,    0, 1634, 1192, -401, -832, 1192, 2066,    0 }, /* YUV601L_TO_RGBF */
	{ 1192,    0,    0,    0, 1166,    0,    0,    0, 1166 }, /* YUV601L_TO_YUV601F */
	{ 1024, -118, -213,    0, 1043,  117,    0,   77, 1050 }, /* YUV601L_TO_YUV709L */
	{ 1192, -138, -248,    0, 1187,  134,    0,   87, 1195 }, /* YUV601L_TO_YUV709F */
	{  879,    0, 1233,  879, -303, -628,  879, 1558,    0 }, /* YUV601F_TO_RGBL */
	{ 1024,    0, 1436, 1024, -352, -731, 1024, 1815,    0 }, /* YUV601F_TO_RGBF */
	{  879,    0,    0,    0,  900,    0,    0,    0,  900 }, /* YUV601F_TO_YUV601L */
	{  879, -104, -187,    0,  916,  103,    0,   68,  922 }, /* YUV601F_TO_YUV709L */
	{ 1024, -121, -218,    0, 1043,  117,    0,   77, 1050 }, /* YUV601F_TO_YUV709F */
	{ 1024,    0, 1577, 1024, -188, -469, 1024, 1858,    0 }, /* YUV709L_TO_RGBL */
	{ 1192,    0, 1836, 1192, -218, -546, 1192, 2163,    0 }, /* YUV709L_TO_RGBF */
	{ 1024,  102,  196,    0, 1014, -113,    0,  -74, 1007 }, /* YUV709L_TO_YUV601L */
	{ 1192,  118,  229,    0, 1154, -129,    0,  -84, 1146 }, /* YUV709L_TO_YUV601F */
	{ 1192,    0,    0,    0, 1166,    0,    0,    0, 1166 }, /* YUV709L_TO_YUV709F */
	{  879,    0, 1385,  879, -165, -412,  879, 1632,    0 }, /* YUV709F_TO_RGBL */
	{ 1024,    0, 1613, 1024, -192, -479, 1024, 1900,    0 }, /* YUV709F_TO_RGBF */
	{  879,   89,  172,    0,  890,  -100,    0,  -65,  885 }, /* YUV709F_TO_YUV601L */
	{ 1024,  104,  201,    0, 1014, -113,    0,  -74, 1007 }, /* YUV709F_TO_YUV601F */
	{  879,    0,    0,    0,  900,    0,    0,    0,  900 }, /* YUV709F_TO_YUV709L */
	{ 1024,    0, 1476, 1024, -165, -572, 1024, 1884,    0 }, /* YUV2020L_TO_RGB2020L */
	{ 1192,    0, 1719, 1192, -192, -666, 1192, 2193,    0 }, /* YUV2020L_TO_RGB2020F */
	{ 1192,    0,    0,    0, 1166,    0,    0,    0, 1166 }, /* YUV2020L_TO_YUV2020F */
	{  879,    0, 1297,  879, -145, -502,  879, 1655,    0 }, /* YUV2020F_TO_RGB2020L */
	{ 1024,    0, 1510, 1024, -169, -585, 1024, 1927,    0 }, /* YUV2020F_TO_RGB2020F */
	{  879,    0,    0,    0,  900,    0,    0,    0,  900 }, /* YUV2020F_TO_YUV2020L */
	{  879,    0,    0,    0,  879,    0,    0,    0,  879 }, /* RGB2020F_TO_RGB2020L */
	{ 1192,    0,    0,    0, 1192,    0,    0,    0, 1192 }, /* RGB2020L_TO_RGB2020F */
	// { 1024,    0,    0,    0, 1024,    0,    0,    0, 1024 }, /* IDENTITY_MODE */
};

static const struct rga_csc_coef g_csc_coefs_10bit_pix_10bit_precision[] = {
    { 1196,    0,    0,    0, 1196,    0,    0,    0, 1196 }, /* RGBL_TO_RGBF */
	{  306,  601,  117, -177, -347,  524,  524, -439,  -85 }, /* RGBL_TO_YUV601L */
	{  358,  702,  136, -202, -396,  598,  598, -501,  -97 }, /* RGBL_TO_YUV601F */
	{  218,  732,   74, -120, -404,  524,  524, -476,  -48 }, /* RGBL_TO_YUV709L */
	{  254,  855,   86, -137, -461,  598,  598, -543,  -55 }, /* RGBL_TO_YUV709F */
	{  269,  694,   61, -146, -377,  524,  524, -482,  -42 }, /* RGB2020L_TO_YUV2020L */
	{  314,  811,   71, -167, -431,  598,  598, -550,  -48 }, /* RGB2020L_TO_YUV2020F */

	{  877,    0,    0,    0,  877,    0,    0,    0,  877 }, /* RGBF_TO_RGBL */
	{  262,  515,  100, -151, -297,  448,  448, -376,  -73 }, /* RGBF_TO_YUV601L */
	{  306,  601,  117, -173, -339,  512,  512, -429,  -83 }, /* RGBF_TO_YUV601F */
	{  186,  627,   63, -103, -346,  448,  448, -407,  -41 }, /* RGBF_TO_YUV709L */
	{  218,  732,   74, -117, -395,  512,  512, -465,  -47 }, /* RGBF_TO_YUV709F */
	{  230,  595,   52, -125, -323,  448,  448, -412,  -36 }, /* RGB2020F_TO_YUV2020L */
	{  269,  694,   61, -143, -369,  512,  512, -471,  -41 }, /* RGB2020F_TO_YUV2020F */

	{ 1024,    0, 1404, 1024, -344, -715, 1024, 1774,    0 }, /* YUV601L_TO_RGBL */
	{ 1196,    0, 1639, 1196, -402, -835, 1196, 2072,    0 }, /* YUV601L_TO_RGBF */
	{ 1196,    0,    0,    0, 1169,    0,    0,    0, 1169 }, /* YUV601L_TO_YUV601F */
	{ 1024, -118, -213,    0, 1043,  117,    0,   77, 1050 }, /* YUV601L_TO_YUV709L */
	{ 1196, -138, -249,    0, 1191,  134,    0,   88, 1199 }, /* YUV601L_TO_YUV709F */

	{  877,    0, 1229,  877, -302, -626,  877, 1554,    0 }, /* YUV601F_TO_RGBL */
	{ 1024,    0, 1436, 1024, -352, -731, 1024, 1815,    0 }, /* YUV601F_TO_RGBF */
	{  877,    0,    0,    0,  897,    0,    0,    0,  897 }, /* YUV601F_TO_YUV601L */
	{  877, -106, -191,    0,  914,  103,    0,   67,  920 }, /* YUV601F_TO_YUV709L */
	{ 1024, -121, -218,    0, 1043,  117,    0,   77, 1050 }, /* YUV601F_TO_YUV709F */

	{ 1024,    0, 1577, 1024, -188, -469, 1024, 1858,    0 }, /* YUV709L_TO_RGBL */
	{ 1196,    0, 1841, 1196, -219, -547, 1196, 2169,    0 }, /* YUV709L_TO_RGBF */
	{ 1024,  104,  201,    0, 1014, -113,    0,  -74, 1007 }, /* YUV709L_TO_YUV601L */
	{ 1196,  119,  229,    0, 1157, -129,    0,  -85, 1150 }, /* YUV709L_TO_YUV601F */
	{ 1196,    0,    0,    0, 1169,    0,    0,    0, 1169 }, /* YUV709L_TO_YUV709F */

	{  877,    0, 1381,  877, -164, -410,  877, 1627,    0 }, /* YUV709F_TO_RGBL */
	{ 1024,    0, 1613, 1024, -192, -479, 1024, 1900,    0 }, /* YUV709F_TO_RGBF */
	{  877,   91,  176,    0,  888,  -99,    0,  -65,  882 }, /* YUV709F_TO_YUV601L */
	{ 1024,  104,  201,    0, 1014, -113,    0,  -74, 1007 }, /* YUV709F_TO_YUV601F */
	{  877,    0,    0,    0,  897,    0,    0,    0,  897 }, /* YUV709F_TO_YUV709L */

	{ 1024,    0, 1476, 1024, -165, -572, 1024, 1884,    0 }, /* YUV2020L_TO_RGB2020L */
	{ 1196,    0, 1724, 1196, -192, -668, 1196, 2200,    0 }, /* YUV2020L_TO_RGB2020F */
	{ 1196,    0,    0,    0, 1169,    0,    0,    0, 1169 }, /* YUV2020L_TO_YUV2020F */

	{  877,    0, 1293,  877, -144, -501,  877, 1650,    0 }, /* YUV2020F_TO_RGB2020L */
	{ 1024,    0, 1510, 1024, -169, -585, 1024, 1927,    0 }, /* YUV2020F_TO_RGB2020F */
	{  877,    0,    0,    0,  897,    0,    0,    0,  897 }, /* YUV2020F_TO_YUV2020L */
	{  877,    0,    0,    0,  877,    0,    0,    0,  877 }, /* RGB2020F_TO_RGB2020L */
	{ 1196,    0,    0,    0, 1196,    0,    0,    0, 1196 }, /* RGB2020L_TO_RGB2020F */

	// { 1024,    0,    0,    0, 1024,    0,    0,    0, 1024 }, /* IDENTITY_MODE */
};

static const struct rga_csc_coef_version g_csc_coef_version_table[] = {
    {8, 8, g_csc_coefs_8bit_pix_8bit_precision},
    {8, 10, g_csc_coefs_8bit_pix_10bit_precision},
    {10, 10, g_csc_coefs_10bit_pix_10bit_precision},
};

static struct rga_csc_clip g_csc_clip_8bit[] = {
    { { 0xff, 0x0}, { 0xff, 0x0} },     //yuv full range Y[255,0] UV[255,0], rgb full range RGB[255,0]
    { { 0xeb, 0x10}, { 0xf0, 0x10} },   //yuv limit range Y[235,16] UV[240,16]
    { { 0xeb, 0x10}, { 0xeb, 0x10} },   //rga limit range RGB[235,16]
};

static struct rga_csc_clip g_csc_clip_10bit[] = {
    { { 0x3ff, 0x0}, { 0x3ff, 0x0} },     //yuv full range Y[1023,0] UV[1023,0], rgb full range RGB[1023,0]
    { { 0x3ac, 0x40}, { 0x3c0, 0x40} },   //yuv limit range Y[940,64] UV[960,64]
    { { 0x3ac, 0x40}, { 0x3ac, 0x40} },   //rga limit range RGB[940,64]
};

static const struct rga_csc_coef brg2rgb_swap_mat = {
    0, 1, 0,
    0, 0, 1,
    1, 0, 0,
};

static const struct rga_csc_coef yuv2vyu_swap_mat = {
    0, 1, 0,
    0, 0, 1,
    1, 0, 0,
};

int rga_setup_color_space_mode(struct rga_csc_channel_info *info, int format, int color_space_mode) {
    if (info == NULL)
        return IM_STATUS_ILLEGAL_PARAM;

    info->color_space = color_space_mode & IM_FULL_CSC_MASK;
    info->is_full_range = is_full_range(info->color_space);
    info->is_yuv = is_yuv_format(format);

    return IM_STATUS_SUCCESS;
}

int rga_setup_default_RGB_color_space_mode(struct rga_csc_channel_info *info) {
    if (info == NULL)
        return IM_STATUS_ILLEGAL_PARAM;

    info->color_space = IM_RGB_FULL_RANGE;
    info->is_full_range = true;
    info->is_yuv = false;

    return IM_STATUS_SUCCESS;
}

const struct rga_csc_coef *rga_get_csc_matrix(struct rga_csc_convert_mode *mode)
{
    int i;
    const struct rga_csc_coef *csc_coefs = NULL;

    if (mode == NULL)
        return NULL;

    /* Search for coef table at different csc precision */
	for (i = 0; i < sizeof(g_csc_coef_version_table) / sizeof(g_csc_coef_version_table[0]); i++) {
		if ((g_csc_coef_version_table[i].pixel_depth == mode->pixel_depth) &&
		    (g_csc_coef_version_table[i].coef_precision == mode->coef_precision)) {
			csc_coefs = g_csc_coef_version_table[i].csc_coefs;
			break;
		}
	}

    if (csc_coefs == NULL) {
        IM_LOGE("Unsupported CSC Matrix version: pixel_depth[%d], coef_precision[%d]",
                 mode->pixel_depth, mode->coef_precision);
        return NULL;
    }

    for (i = 0; i < sizeof(g_csc_mode_table) / sizeof(g_csc_mode_table[0]); i++) {
        if (g_csc_mode_table[i].input == mode->input.color_space &&
            g_csc_mode_table[i].output == mode->output.color_space)
            return &csc_coefs[i];
    }

    IM_LOGE("Unsupported CSC mode: [%s(%#x)] -> [%s(%#x)]",
            string_color_space(mode->input.color_space), mode->input.color_space,
            string_color_space(mode->output.color_space), mode->output.color_space);

    return NULL;
}

static void rga_csc_matrix_multiply(struct rga_csc_coef *dst, const struct rga_csc_coef *m0,
                const struct rga_csc_coef *m1)
{
    dst->coef00 = m0->coef00 * m1->coef00 +
                  m0->coef01 * m1->coef10 +
                  m0->coef02 * m1->coef20;

    dst->coef01 = m0->coef00 * m1->coef01 +
                  m0->coef01 * m1->coef11 +
                  m0->coef02 * m1->coef21;

    dst->coef02 = m0->coef00 * m1->coef02 +
                  m0->coef01 * m1->coef12 +
                  m0->coef02 * m1->coef22;

    dst->coef10 = m0->coef10 * m1->coef00 +
                  m0->coef11 * m1->coef10 +
                  m0->coef12 * m1->coef20;

    dst->coef11 = m0->coef10 * m1->coef01 +
                  m0->coef11 * m1->coef11 +
                  m0->coef12 * m1->coef21;

    dst->coef12 = m0->coef10 * m1->coef02 +
                  m0->coef11 * m1->coef12 +
                  m0->coef12 * m1->coef22;

    dst->coef20 = m0->coef20 * m1->coef00 +
                  m0->coef21 * m1->coef10 +
                  m0->coef22 * m1->coef20;

    dst->coef21 = m0->coef20 * m1->coef01 +
                  m0->coef21 * m1->coef11 +
                  m0->coef22 * m1->coef21;

    dst->coef22 = m0->coef20 * m1->coef02 +
                  m0->coef21 * m1->coef12 +
                  m0->coef22 * m1->coef22;
}

static void rga_csc_matrix_vector_multiply(struct rga_csc_vector *dst,
                     const struct rga_csc_coef *m0,
                     const struct rga_csc_vector *v0)
{
    dst->offset0 = m0->coef00 * v0->offset0 +
                   m0->coef01 * v0->offset1 +
                   m0->coef02 * v0->offset2;

    dst->offset1 = m0->coef10 * v0->offset0 +
                   m0->coef11 * v0->offset1 +
                   m0->coef12 * v0->offset2;

    dst->offset2 = m0->coef20 * v0->offset0 +
                   m0->coef21 * v0->offset1 +
                   m0->coef22 * v0->offset2;
}

static void rga_get_csc_range_offset(struct rga_csc_convert_mode *convert_mode, struct rga_csc_dc_coef *dc_coef) {
    int offset_y, offset_c, offset_shift_bits;

    offset_shift_bits = convert_mode->pixel_depth - 8;

    offset_y = convert_mode->input.is_full_range ? 0 : 16;
    offset_c = convert_mode->input.is_yuv ? 128 : offset_y;
    dc_coef->in_dc0 = -(offset_y << offset_shift_bits);
    dc_coef->in_dc1 = -(offset_c << offset_shift_bits);
    dc_coef->in_dc2 = -(offset_c << offset_shift_bits);

    offset_y = convert_mode->output.is_full_range ? 0 : 16;
    offset_c = convert_mode->output.is_yuv ? 128 : offset_y;
    dc_coef->out_dc0 = offset_y << offset_shift_bits;
    dc_coef->out_dc1 = offset_c << offset_shift_bits;
    dc_coef->out_dc2 = offset_c << offset_shift_bits;
}

static void rga_calc_output_coef(struct rga_csc_convert_mode *convert_mode,
                                const struct rga_csc_coef *matrix,
                                struct rga_csc_coef *out_matrix,  struct rga_csc_vector *out_vector) {
    struct rga_csc_dc_coef dc_coef;
    struct rga_csc_vector dc_in_vector;
    struct rga_csc_vector dc_out_vector;
    struct rga_csc_vector v;

    memset(&dc_coef, 0, sizeof(dc_coef));
    rga_get_csc_range_offset(convert_mode, &dc_coef);

    out_matrix->coef00 = matrix->coef00;
    out_matrix->coef01 = matrix->coef01;
    out_matrix->coef02 = matrix->coef02;
    out_matrix->coef10 = matrix->coef10;
    out_matrix->coef11 = matrix->coef11;
    out_matrix->coef12 = matrix->coef12;
    out_matrix->coef20 = matrix->coef20;
    out_matrix->coef21 = matrix->coef21;
    out_matrix->coef22 = matrix->coef22;

    dc_in_vector.offset0 = dc_coef.in_dc0;
    dc_in_vector.offset1 = dc_coef.in_dc1;
    dc_in_vector.offset2 = dc_coef.in_dc2;
    dc_out_vector.offset0 = dc_coef.out_dc0;
    dc_out_vector.offset1 = dc_coef.out_dc1;
    dc_out_vector.offset2 = dc_coef.out_dc2;

    rga_csc_matrix_vector_multiply(&v, matrix, &dc_in_vector);
    out_vector->offset0 = v.offset0 + (dc_out_vector.offset0 << convert_mode->coef_precision);
    out_vector->offset1 = v.offset1 + (dc_out_vector.offset1 << convert_mode->coef_precision);
    out_vector->offset2 = v.offset2 + (dc_out_vector.offset2 << convert_mode->coef_precision);
}

/*
 * Convert user CSC formula order to hardware channel order.
 * User matrix is expressed in canonical order:
 *   RGB side: [R, G, B]
 *   YUV side: [Y, U, V]
 * Hardware CSC is designed as RGB2YUV, channel index is fixed as [V/R, Y/G, U/B].
 * So convert with:
 *   M_hw = O * M_user * I
 *   b_hw = O * b_user
 * where:
 *   I = yuv2vyu for YUV input (convert user YUV coeff to HW VYU input order)
 *   O = brg2rgb for RGB output (convert HW BRG lane order to user RGB output lane order)
 *     for example, R = coe_0 * [RGB/VYU], but coe_0 was originally intended to be
 *   used for calculating the Y channel.
 */
static void rga_csc_swap_color_channel(const struct rga_csc_convert_mode *mode,
                                       struct rga_csc_coef *matrix,
                                       struct rga_csc_vector *vector) {
    struct rga_csc_coef tmp_matrix;
    struct rga_csc_vector tmp_vector;

    if (mode->input.is_yuv) {
        memcpy(&tmp_matrix, matrix, sizeof(tmp_matrix));
        rga_csc_matrix_multiply(matrix, &tmp_matrix, &yuv2vyu_swap_mat);
    }

    if (!mode->output.is_yuv) {
        memcpy(&tmp_matrix, matrix, sizeof(tmp_matrix));
        rga_csc_matrix_multiply(matrix, &brg2rgb_swap_mat, &tmp_matrix);

        memcpy(&tmp_vector, vector, sizeof(tmp_vector));
        rga_csc_matrix_vector_multiply(vector, &brg2rgb_swap_mat, &tmp_vector);
    }
}

static void rga_csc_setup_full_csc(struct rga_req *msg, struct rga_csc_convert_mode *convert_mode,
                                   struct rga_csc_coef *matrix, struct rga_csc_vector *vector,
                                   const struct rga_csc_clip *clip) {
    full_csc_t *full_csc;
    struct rga_csc_clip *csc_clip;

    full_csc = &msg->full_csc;
    csc_clip = &msg->full_csc_clip;

    full_csc->flag = 1;

    full_csc->coe_y.r_v = matrix->coef00;
    full_csc->coe_y.g_y = matrix->coef01;
    full_csc->coe_y.b_u = matrix->coef02;
    full_csc->coe_y.off = vector->offset0 + (1 << (convert_mode->coef_precision - 1));
    full_csc->coe_u.r_v = matrix->coef10;
    full_csc->coe_u.g_y = matrix->coef11;
    full_csc->coe_u.b_u = matrix->coef12;
    full_csc->coe_u.off = vector->offset1 + (1 << (convert_mode->coef_precision - 1));
    full_csc->coe_v.r_v = matrix->coef20;
    full_csc->coe_v.g_y = matrix->coef21;
    full_csc->coe_v.b_u = matrix->coef22;
    full_csc->coe_v.off = vector->offset2 + (1 << (convert_mode->coef_precision - 1));

    /* set clip range */
    if (convert_mode->output.is_full_range)
        *csc_clip = clip[0];
    else
        *csc_clip = convert_mode->output.is_yuv ? clip[1] : clip[2];

    msg->feature.full_csc_clip_en = true;
}

static int rga_csc_check_coef(const struct rga_csc_coef *coef, struct rga_csc_convert_mode *convert_mode) {
    int i;
    int32_t coef_array[9];
    int64_t coef_abs;
    int64_t coef_integer;

    coef_array[0] = coef->coef00;
    coef_array[1] = coef->coef01;
    coef_array[2] = coef->coef02;
    coef_array[3] = coef->coef10;
    coef_array[4] = coef->coef11;
    coef_array[5] = coef->coef12;
    coef_array[6] = coef->coef20;
    coef_array[7] = coef->coef21;
    coef_array[8] = coef->coef22;

    for (i = 0; i < 9; i++) {
        coef_abs = (int64_t)coef_array[i];
        coef_abs = (coef_abs < 0) ? -coef_abs : coef_abs;
        coef_integer = coef_abs >> convert_mode->coef_precision;

        if (coef_integer > convert_mode->coef_integer) {
            IM_LOGE("Unsupported CSC mode: [%s(%#x)] -> [%s(%#x)], coef[%#x(%d)] with integer parts[%#llx] greater than %d are not supported.",
                 string_color_space(convert_mode->input.color_space),
                 convert_mode->input.color_space,
                 string_color_space(convert_mode->output.color_space),
                 convert_mode->output.color_space,
                 coef_array[i], coef_array[i],
                 (unsigned long long)coef_integer,
                 convert_mode->coef_integer);
            return IM_STATUS_NOT_SUPPORTED;
        }
    }

    return IM_STATUS_SUCCESS;
}

int rga_csc_setup_matrix(struct rga_req *msg, struct rga_csc_convert_mode *convert_mode) {
    int ret;
    const struct rga_csc_coef *csc_coef;
    const struct rga_csc_clip *clip;
    struct rga_csc_coef matrix;
    struct rga_csc_vector vector;

    if (msg == NULL || convert_mode == NULL) {
        IM_LOGE("%s, Invalid parameter: [%p, %p]", __func__, msg, convert_mode);
        return IM_STATUS_ILLEGAL_PARAM;
    }

    if (convert_mode->input.color_space == convert_mode->output.color_space)
        return IM_STATUS_SUCCESS;

    //find csc mode
    csc_coef = rga_get_csc_matrix(convert_mode);
    if (csc_coef == NULL)
        return IM_STATUS_NOT_SUPPORTED;

    /* Validate CSC coefficients against hardware constraints */
    ret = rga_csc_check_coef(csc_coef, convert_mode);
    if (ret != IM_STATUS_SUCCESS)
        return ret;

    rga_calc_output_coef(convert_mode, csc_coef, &matrix, &vector);

    rga_csc_swap_color_channel(convert_mode, &matrix, &vector);

    if (convert_mode->pixel_depth == 10)
        clip = g_csc_clip_10bit;
    else
        clip = g_csc_clip_8bit;

    rga_csc_setup_full_csc(msg, convert_mode, &matrix, &vector, clip);

    return IM_STATUS_SUCCESS;
}

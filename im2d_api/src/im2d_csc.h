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

#ifndef _RGA_IM2D_CSC_H_
#define _RGA_IM2D_CSC_H_

#include <stdint.h>
#include <stdbool.h>

#include "im2d_type.h"

struct rga_csc_channel_info {
    uint32_t color_space;
    bool is_yuv;
    bool is_full_range;
};

struct rga_csc_convert_mode {
    struct rga_csc_channel_info input;
    struct rga_csc_channel_info output;

    uint8_t pixel_depth;
    uint8_t coef_integer;
    uint8_t coef_precision;
};

int rga_setup_color_space_mode(struct rga_csc_channel_info *info, int format, int color_space_mode);
int rga_setup_default_RGB_color_space_mode(struct rga_csc_channel_info *info);

int rga_csc_setup_matrix(struct rga_req *msg, struct rga_csc_convert_mode *convert_mode);

#endif /*#ifndef _RGA_IM2D_CSC_H_*/

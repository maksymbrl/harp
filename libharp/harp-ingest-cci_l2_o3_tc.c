/*
 * Copyright (C) 2015-2025 S[&]T, The Netherlands.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "coda.h"
#include "harp-constants.h"
#include "harp-ingestion.h"

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ingest_info_struct
{
    coda_product *product;
    long num_time;
    long num_layers;
    int has_transposed_dims;
} ingest_info;

static int init_dimensions(ingest_info *info)
{
    coda_cursor cursor;
    long coda_dim[CODA_MAX_NUM_DIMS];
    int num_coda_dims;

    if (coda_cursor_set_product(&cursor, info->product) != 0)
    {
        harp_set_error(HARP_ERROR_CODA, NULL);
        return -1;
    }
    if (coda_cursor_goto(&cursor, "/averaging_kernels") != 0)
    {
        harp_set_error(HARP_ERROR_CODA, NULL);
        return -1;
    }
    if (coda_cursor_get_array_dim(&cursor, &num_coda_dims, coda_dim) != 0)
    {
        harp_set_error(HARP_ERROR_CODA, NULL);
        return -1;
    }
    if (num_coda_dims < 2 || num_coda_dims > 3)
    {
        harp_set_error(HARP_ERROR_INGESTION, "dataset has %d dimensions, expected either 2 or 3", num_coda_dims);
        harp_add_coda_cursor_path_to_error_message(&cursor);
        return -1;
    }

    if (num_coda_dims == 2)
    {
        info->num_time = coda_dim[0];
        info->num_layers = coda_dim[1];
        info->has_transposed_dims = 0;
    }
    else
    {
        info->num_time = coda_dim[1] * coda_dim[2];
        info->num_layers = coda_dim[0];
        info->has_transposed_dims = 1;
    }

    return 0;
}

static void ingestion_done(void *user_data)
{
    free(user_data);
}

static int ingestion_init(const harp_ingestion_module *module, coda_product *product,
                          const harp_ingestion_options *options, harp_product_definition **definition, void **user_data)
{
    ingest_info *info;

    (void)options;

    info = malloc(sizeof(ingest_info));
    if (info == NULL)
    {
        harp_set_error(HARP_ERROR_OUT_OF_MEMORY, "out of memory (could not allocate %lu bytes) (%s:%u)",
                       sizeof(ingest_info), __FILE__, __LINE__);
        return -1;
    }

    info->product = product;

    if (init_dimensions(info) != 0)
    {
        ingestion_done(info);
        return -1;
    }

    *definition = *module->product_definition;
    *user_data = info;

    return 0;
}

static int read_dataset(ingest_info *info, const char *path, harp_data_type data_type, long num_elements,
                        harp_array data)
{
    coda_cursor cursor;
    long coda_num_elements;
    harp_scalar fill_value;

    if (coda_cursor_set_product(&cursor, info->product) != 0)
    {
        harp_set_error(HARP_ERROR_CODA, NULL);
        return -1;
    }
    if (coda_cursor_goto(&cursor, path) != 0)
    {
        harp_set_error(HARP_ERROR_CODA, NULL);
        return -1;
    }
    if (coda_cursor_get_num_elements(&cursor, &coda_num_elements) != 0)
    {
        harp_set_error(HARP_ERROR_CODA, NULL);
        return -1;
    }
    if (coda_num_elements != num_elements)
    {
        harp_set_error(HARP_ERROR_INGESTION, "dataset has %ld elements (expected %ld)", coda_num_elements,
                       num_elements);
        harp_add_coda_cursor_path_to_error_message(&cursor);
        harp_add_error_message(" (%s:%lu)", __FILE__, __LINE__);
        return -1;
    }
    switch (data_type)
    {
        case harp_type_int32:
            {
                if (coda_cursor_read_int32_array(&cursor, data.int32_data, coda_array_ordering_c) != 0)
                {
                    harp_set_error(HARP_ERROR_CODA, NULL);
                    return -1;
                }
                if (coda_cursor_goto(&cursor, "@FillValue[0]") == 0)
                {
                    if (coda_cursor_read_int32(&cursor, &fill_value.int32_data) != 0)
                    {
                        harp_set_error(HARP_ERROR_CODA, NULL);
                        return -1;
                    }
                    harp_array_replace_fill_value(data_type, num_elements, data, fill_value);
                }
            }
            break;
        case harp_type_double:
            {
                if (coda_cursor_read_double_array(&cursor, data.double_data, coda_array_ordering_c) != 0)
                {
                    harp_set_error(HARP_ERROR_CODA, NULL);
                    return -1;
                }
                if (coda_cursor_goto(&cursor, "@FillValue[0]") == 0)
                {
                    if (coda_cursor_read_double(&cursor, &fill_value.double_data) != 0)
                    {
                        harp_set_error(HARP_ERROR_CODA, NULL);
                        return -1;
                    }
                    harp_array_replace_fill_value(data_type, num_elements, data, fill_value);
                }
            }
            break;
        default:
            assert(0);
            exit(1);
    }

    return 0;
}

static int read_dimensions(void *user_data, long dimension[HARP_NUM_DIM_TYPES])
{
    ingest_info *info = (ingest_info *)user_data;

    dimension[harp_dimension_time] = info->num_time;
    dimension[harp_dimension_vertical] = info->num_layers;

    return 0;
}

static int read_datetime(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    return read_dataset(info, "/time", harp_type_double, info->num_time, data);
}

static int read_scan_subindex(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    return read_dataset(info, "/pixel_number", harp_type_int32, info->num_time, data);
}

static int read_longitude(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    return read_dataset(info, "/longitude", harp_type_double, info->num_time, data);
}

static int read_latitude(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    return read_dataset(info, "/latitude", harp_type_double, info->num_time, data);
}

static int read_longitude_bounds(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    if (read_dataset(info, "/longitude_corner", harp_type_double, info->num_time * 4, data) != 0)
    {
        return -1;
    }

    if (info->has_transposed_dims)
    {
        long dimension[2];

        /* Re-order array dimensions from [4, num_time] to [num_time, 4]. */
        dimension[0] = 4;
        dimension[1] = info->num_time;
        if (harp_array_transpose(harp_type_double, 2, dimension, NULL, data) != 0)
        {
            return -1;
        }
    }

    return 0;
}

static int read_latitude_bounds(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    if (read_dataset(info, "/latitude_corner", harp_type_double, info->num_time * 4, data) != 0)
    {
        return -1;
    }

    if (info->has_transposed_dims)
    {
        long dimension[2];

        /* Re-order array dimensions from [4, num_time] to [num_time, 4]. */
        dimension[0] = 4;
        dimension[1] = info->num_time;
        if (harp_array_transpose(harp_type_double, 2, dimension, NULL, data) != 0)
        {
            return -1;
        }
    }

    return 0;
}

static int read_solar_zenith_angle(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    return read_dataset(info, "/solar_zenith_angle", harp_type_double, info->num_time, data);
}

static int read_sensor_zenith_angle(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    return read_dataset(info, "/viewing_zenith_angle", harp_type_double, info->num_time, data);
}

static int read_relative_azimuth_angle(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    return read_dataset(info, "/relative_azimuth_angle", harp_type_double, info->num_time, data);
}

static int read_pressure_bounds(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;
    long i;

    if (read_dataset(info, "/atmosphere_pressure_grid", harp_type_double, info->num_time * (info->num_layers + 1),
                     data) != 0)
    {
        return -1;
    }

    if (info->has_transposed_dims)
    {
        long dimension[2];

        /* Re-order array dimensions from [num_layers + 1, num_time] to [num_time, num_layers + 1]. */
        dimension[0] = info->num_layers + 1;
        dimension[1] = info->num_time;
        if (harp_array_transpose(harp_type_double, 2, dimension, NULL, data) != 0)
        {
            return -1;
        }
    }

    /* Convert from num_levels (== num_layers + 1) consecutive pressures to num_layers x 2 pressure bounds. Iterate in
     * reverse to ensure correct results (conversion is performed in place).
     */
    for (i = info->num_time - 1; i >= 0; --i)
    {
        double *pressure = &data.double_data[i * (info->num_layers + 1)];
        double *pressure_bounds = &data.double_data[i * info->num_layers * 2];
        long j;

        for (j = info->num_layers - 1; j >= 0; --j)
        {
            /* NB. The order of the following two lines is important to ensure correct results. */
            pressure_bounds[j * 2 + 1] = pressure[j + 1];
            pressure_bounds[j * 2] = pressure[j];
        }
    }

    return 0;
}

static int read_temperature(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    return read_dataset(info, "/effective_temperature", harp_type_double, info->num_time, data);
}

static int read_O3_column_number_density(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    return read_dataset(info, "/total_ozone_column", harp_type_double, info->num_time, data);
}

static int read_O3_column_number_density_uncertainty_random(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    return read_dataset(info, "/total_ozone_column_random_error", harp_type_double, info->num_time, data);
}

static int read_O3_column_number_density_avk(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    if (read_dataset(info, "/averaging_kernels", harp_type_double, info->num_time * info->num_layers, data) != 0)
    {
        return -1;
    }

    if (info->has_transposed_dims)
    {
        long dimension[2];

        /* Re-order array dimensions from [num_layers, num_time] to [num_time, num_layers]. */
        dimension[0] = info->num_layers;
        dimension[1] = info->num_time;
        if (harp_array_transpose(harp_type_double, 2, dimension, NULL, data) != 0)
        {
            return -1;
        }
    }

    return 0;
}

static int read_O3_column_number_density_apriori(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    if (read_dataset(info, "/apriori_ozone_profile", harp_type_double, info->num_time * info->num_layers, data) != 0)
    {
        return -1;
    }

    if (info->has_transposed_dims)
    {
        long dimension[2];

        /* Re-order array dimensions from [num_layers, num_time] to [num_time, num_layers]. */
        dimension[0] = info->num_layers;
        dimension[1] = info->num_time;
        if (harp_array_transpose(harp_type_double, 2, dimension, NULL, data) != 0)
        {
            return -1;
        }
    }

    return 0;
}

static int read_cloud_fraction(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    return read_dataset(info, "/cloud_fraction", harp_type_double, info->num_time, data);
}

static int read_cloud_top_pressure(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    return read_dataset(info, "/cloud_top_pressure", harp_type_double, info->num_time, data);
}

static int read_cloud_top_albedo(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    return read_dataset(info, "/cloud_albedo", harp_type_double, info->num_time, data);
}

static int read_scene_albedo(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    return read_dataset(info, "/effective_scene_albedo", harp_type_double, info->num_time, data);
}

static int read_scene_pressure(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    return read_dataset(info, "/effective_scene_pressure", harp_type_double, info->num_time, data);
}

static int read_surface_albedo(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    return read_dataset(info, "/surface_albedo", harp_type_double, info->num_time, data);
}

static int read_surface_altitude(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    return read_dataset(info, "/surface_altitude", harp_type_double, info->num_time, data);
}

int harp_ingestion_module_cci_l2_o3_tc_init(void)
{
    harp_ingestion_module *module;
    harp_product_definition *product_definition;
    harp_variable_definition *variable_definition;
    harp_dimension_type dimension_type[2] = { harp_dimension_time, harp_dimension_vertical };
    harp_dimension_type bounds_dimension_type[2] = { harp_dimension_time, harp_dimension_independent };
    long bounds_dimension[2] = { -1, 4 };
    harp_dimension_type pressure_bounds_dimension_type[3] = { harp_dimension_time, harp_dimension_vertical,
        harp_dimension_independent
    };
    long pressure_bounds_dimension[3] = { -1, -1, 2 };
    const char *description;
    const char *path;

    module = harp_ingestion_register_module("ESACCI_OZONE_L2_TC", "Ozone CCI", "ESACCI_OZONE", "L2_TC",
                                            "CCI L2 O3 total column", ingestion_init, ingestion_done);

    /* ESACCI_OZONE_L2_TC product */
    product_definition = harp_ingestion_register_product(module, "ESACCI_OZONE_L2_TC", NULL, read_dimensions);

    /* scan_subindex */
    description = "zero-based index of the instantaneous field of view within the swath";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "scan_subindex", harp_type_int32, 1,
                                                   dimension_type, NULL, description, NULL, NULL, read_scan_subindex);
    path = "/pixel_number[]";
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);

    /* datetime */
    description = "time of the measurement";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "datetime", harp_type_double, 1, dimension_type,
                                                   NULL, description, "days since 1995-01-01", NULL, read_datetime);
    path = "/time[]";
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);

    /* longitude */
    description = "longitude of the ground pixel center";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "longitude", harp_type_double, 1, dimension_type,
                                                   NULL, description, "degree_east", NULL, read_longitude);
    harp_variable_definition_set_valid_range_double(variable_definition, -180.0f, 180.0f);
    path = "/longitude[]";
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);

    /* latitude */
    description = "latitude of the ground pixel center";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "latitude", harp_type_double, 1, dimension_type,
                                                   NULL, description, "degree_north", NULL, read_latitude);
    harp_variable_definition_set_valid_range_double(variable_definition, -90.0f, 90.0f);
    path = "/latitude[]";
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);

    /* longitude_bounds */
    description = "longitudes of the ground pixel corners";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "longitude_bounds", harp_type_double, 2,
                                                   bounds_dimension_type, bounds_dimension, description, "degree_east",
                                                   NULL, read_longitude_bounds);
    harp_variable_definition_set_valid_range_double(variable_definition, -180.0f, 180.0f);
    path = "/longitude_corner[]";
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);

    /* latitude_bounds */
    description = "latitudes of the ground pixel corners";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "latitude_bounds", harp_type_double, 2,
                                                   bounds_dimension_type, bounds_dimension, description, "degree_north",
                                                   NULL, read_latitude_bounds);
    harp_variable_definition_set_valid_range_double(variable_definition, -90.0f, 90.0f);
    path = "/latitude_corner[]";
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);

    /* sensor_zenith_angle */
    description = "zenith angle of the sensor at the ground pixel center (< 0 for Eastern, > 0 for Western pixels)";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "sensor_zenith_angle", harp_type_double, 1,
                                                   dimension_type, NULL, description, "degree", NULL,
                                                   read_sensor_zenith_angle);
    harp_variable_definition_set_valid_range_double(variable_definition, 0.0, 60.0);
    path = "/viewing_zenith_angle[]";
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);

    /* solar_zenith_angle */
    description = "zenith angle of the Sun at the ground pixel center";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "solar_zenith_angle", harp_type_double, 1,
                                                   dimension_type, NULL, description, "degree", NULL,
                                                   read_solar_zenith_angle);
    harp_variable_definition_set_valid_range_double(variable_definition, 0.0, 90.0);
    path = "/solar_zenith_angle[]";
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);

    /* relative_azimuth_angle */
    description = "relative azimuth angle at the ground pixel center";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "relative_azimuth_angle", harp_type_double, 1,
                                                   dimension_type, NULL, description, "degree", NULL,
                                                   read_relative_azimuth_angle);
    path = "/relative_azimuth_angle[]";
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);

    /* O3_column_number_density */
    description = "O3 total column number density";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "O3_column_number_density", harp_type_double, 1,
                                                   dimension_type, NULL, description, "DU", NULL,
                                                   read_O3_column_number_density);
    path = "/total_ozone_column[]";
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);

    /* O3_column_number_density_uncertainty_random */
    description = "random uncertainty of the O3 total column number density";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "O3_column_number_density_uncertainty_random",
                                                   harp_type_double, 1, dimension_type, NULL, description, "DU", NULL,
                                                   read_O3_column_number_density_uncertainty_random);
    path = "/total_ozone_column_random_error[]";
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);

    /* temperature */
    description = "retrieved effective temperature";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "temperature", harp_type_double, 1,
                                                   dimension_type, NULL, description, "K", NULL, read_temperature);
    path = "/effective_temperature[]";
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);

    /* cloud_fraction */
    description = "effective cloud fraction";
    variable_definition = harp_ingestion_register_variable_full_read(product_definition, "cloud_fraction",
                                                                     harp_type_double, 1, dimension_type, NULL,
                                                                     description, HARP_UNIT_DIMENSIONLESS, NULL,
                                                                     read_cloud_fraction);
    path = "/cloud_fraction[]";
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);

    /* cloud_top_pressure */
    description = "cloud top pressure";
    variable_definition = harp_ingestion_register_variable_full_read(product_definition, "cloud_top_pressure",
                                                                     harp_type_double, 1, dimension_type, NULL,
                                                                     description, "Pa", NULL, read_cloud_top_pressure);
    path = "/cloud_top_pressure[]";
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);

    /* cloud_top_albedo */
    description = "effective cloud top albedo";
    variable_definition = harp_ingestion_register_variable_full_read(product_definition, "cloud_top_albedo",
                                                                     harp_type_double, 1, dimension_type, NULL,
                                                                     description, HARP_UNIT_DIMENSIONLESS, NULL,
                                                                     read_cloud_top_albedo);
    path = "/cloud_albedo[]";
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);

    /* scene_pressure */
    description = "pressure at the effective scene used for the retrieval";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "scene_pressure", harp_type_double, 1,
                                                   dimension_type, NULL, description, "Pa", NULL, read_scene_pressure);
    path = "/effective_scene_pressure[]";
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);

    /* scene_albedo */
    description = "retrieved effective albedo of the scene";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "scene_albedo", harp_type_double, 1,
                                                   dimension_type, NULL, description, HARP_UNIT_DIMENSIONLESS, NULL,
                                                   read_scene_albedo);
    path = "/effective_scene_albedo[]";
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);

    /* surface_albedo */
    description = "minimum surface albedo at 335nm from OMI LER climatology";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "surface_albedo", harp_type_double, 1,
                                                   dimension_type, NULL, description, HARP_UNIT_DIMENSIONLESS, NULL,
                                                   read_surface_albedo);
    path = "/surface_albedo[]";
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);

    /* surface_altitude */
    description = "surface altitude extracted from GTOPO30";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "surface_altitude", harp_type_double, 1,
                                                   dimension_type, NULL, description, "m", NULL, read_surface_altitude);
    path = "/surface_altitude[]";
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);

    /* pressure_bounds */
    description = "pressure at the boundaries of the layers used in the forward model";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "pressure_bounds", harp_type_double, 3,
                                                   pressure_bounds_dimension_type, pressure_bounds_dimension,
                                                   description, "Pa", NULL, read_pressure_bounds);
    path = "/atmosphere_pressure_grid";
    description = "converted from pressure levels given at the boundaries between adjacent layers to a pair of "
        "pressures per layer (each pair consists of the pressure at the lower and at the upper boundary of a layer)";
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, description);

    /* O3_column_number_density_avk */
    description = "averaging kernels in the layers of the forward model";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "O3_column_number_density_avk", harp_type_double,
                                                   2, dimension_type, NULL, description, HARP_UNIT_DIMENSIONLESS, NULL,
                                                   read_O3_column_number_density_avk);
    path = "/averaging_kernels[]";
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);

    /* O3_column_number_density_apriori */
    description = "a-priori partial ozone columns in the layers of the forward model";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "O3_column_number_density_apriori",
                                                   harp_type_double, 2, dimension_type, NULL, description, "DU", NULL,
                                                   read_O3_column_number_density_apriori);
    path = "/apriori_ozone_profile[]";
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);

    return 0;
}

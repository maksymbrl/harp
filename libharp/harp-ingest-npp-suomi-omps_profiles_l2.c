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
#include "harp-ingestion.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------- Defines ------------------ */

#define CHECKED_MALLOC(v, s) v = malloc(s); if (v == NULL) { harp_set_error(HARP_ERROR_OUT_OF_MEMORY, "out of memory (could not allocate %lu bytes) (%s:%u)", s, __FILE__, __LINE__); return -1;}

#define MICROSECONDS_IN_SECOND                 1000000
#define SECONDS_FROM_1958_TO_2000           1325376000

#define LATLONG_DATA_INCL_CORNERS                    5  /* 1 latitude/longitude value + 4 lat/long corners */

/* ------------------ Typedefs ------------------ */

typedef struct ingest_info_struct
{
    coda_product *product;
    coda_cursor geo_cursor;
    coda_cursor data_cursor;
    long num_times;
    long num_pressures;
    long unused_geo_data_factor;
} ingest_info;

/* -------------- Global variables --------------- */

static double nan;

/* -------------------- Code -------------------- */

static void ingestion_done(void *user_data)
{
    ingest_info *info = (ingest_info *)user_data;

    if (info != NULL)
    {
        free(info);
    }
}

static int read_variable(coda_cursor *cursor, const char *name, int num_dimensions, long *dimensions,
                         double error_range_start, double error_range_end, harp_array data)
{
    double *double_data;
    long num_elements, i;
    long coda_dimension[CODA_MAX_NUM_DIMS];
    int num_coda_dimensions;

    if (coda_cursor_goto_record_field_by_name(cursor, name) != 0)
    {
        harp_set_error(HARP_ERROR_CODA, NULL);
        return -1;
    }
    if (coda_cursor_get_array_dim(cursor, &num_coda_dimensions, coda_dimension) != 0)
    {
        harp_set_error(HARP_ERROR_CODA, NULL);
        return -1;
    }
    if (num_coda_dimensions != num_dimensions)
    {
        harp_set_error(HARP_ERROR_INGESTION,
                       "product error detected in NPP Suomi L2 product (variable %s has %d dimensions, " "expected %d)",
                       name, num_coda_dimensions, num_dimensions);
        return -1;
    }
    num_elements = 1;
    for (i = 0; i < num_dimensions; i++)
    {
        if (dimensions[i] != coda_dimension[i])
        {
            harp_set_error(HARP_ERROR_INGESTION,
                           "product error detected in NPP Suomi L2 product (dimension %ld for variable %s "
                           "has %ld elements, expected %ld", i + 1, name, coda_dimension[i], dimensions[i]);
            return -1;
        }
        num_elements *= coda_dimension[i];
    }
    if (coda_cursor_read_double_array(cursor, data.double_data, coda_array_ordering_c) != 0)
    {
        harp_set_error(HARP_ERROR_CODA, NULL);
        return -1;
    }

    if (error_range_start <= error_range_end)
    {
        double_data = data.double_data;
        for (i = 0; i < num_elements; i++)
        {
            if ((*double_data >= error_range_start) && (*double_data <= error_range_end))
            {
                *double_data = nan;
            }
            double_data++;
        }
    }

    coda_cursor_goto_parent(cursor);

    return 0;
}

static int read_dimensions(void *user_data, long dimension[HARP_NUM_DIM_TYPES])
{
    ingest_info *info = (ingest_info *)user_data;

    /* Note: Do not set an independent dimension here, that causes memory errors */
    dimension[harp_dimension_time] = info->num_times;
    dimension[harp_dimension_vertical] = info->num_pressures;

    return 0;
}

static int read_datetime(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;
    harp_array middle_times;
    double *double_data, value;
    long i, dimensions[1];

    dimensions[0] = info->unused_geo_data_factor * info->num_times;
    CHECKED_MALLOC(middle_times.double_data, dimensions[0] * sizeof(double));
    if (read_variable(&info->geo_cursor, "MidTime", 1, dimensions, -999.5, -992.5, middle_times) != 0)
    {
        free(middle_times.double_data);
        return -1;
    }

    double_data = data.double_data;
    for (i = 0; i < info->num_times; i++)
    {
        value = middle_times.double_data[info->unused_geo_data_factor * i];
        if (!coda_isNaN(value))
        {
            *double_data = (value / MICROSECONDS_IN_SECOND) - SECONDS_FROM_1958_TO_2000;
        }
        else
        {
            *double_data = nan;
        }
        double_data++;
    }

    free(middle_times.double_data);
    return 0;
}

static int read_latitude(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;
    harp_array latitudes_plus_corners;
    double *double_data;
    long i, dimensions[3];

    dimensions[0] = info->num_times;
    dimensions[1] = 1;
    dimensions[2] = LATLONG_DATA_INCL_CORNERS;
    CHECKED_MALLOC(latitudes_plus_corners.double_data, dimensions[0] * dimensions[1] * dimensions[2] * sizeof(double));
    if (read_variable(&info->data_cursor, "latitude_v8", 3, dimensions, -999.95, -999.25, latitudes_plus_corners) != 0)
    {
        free(latitudes_plus_corners.double_data);
        return -1;
    }

    double_data = data.double_data;
    for (i = 0; i < info->num_times; i++)
    {
        *double_data = latitudes_plus_corners.double_data[LATLONG_DATA_INCL_CORNERS * i];
        double_data++;
    }
    return 0;
}

static int read_longitude(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;
    harp_array longitudes_plus_corners;
    double *double_data;
    long i, dimensions[3];

    dimensions[0] = info->num_times;
    dimensions[1] = 1;
    dimensions[2] = LATLONG_DATA_INCL_CORNERS;
    CHECKED_MALLOC(longitudes_plus_corners.double_data, dimensions[0] * dimensions[1] * dimensions[2] * sizeof(double));
    if (read_variable(&info->data_cursor, "longitude_v8", 3, dimensions, -999.95, -999.25, longitudes_plus_corners) !=
        0)
    {
        free(longitudes_plus_corners.double_data);
        return -1;
    }

    double_data = data.double_data;
    for (i = 0; i < info->num_times; i++)
    {
        *double_data = longitudes_plus_corners.double_data[LATLONG_DATA_INCL_CORNERS * i];
        double_data++;
    }
    return 0;
}

static int read_latitude_bounds(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;
    harp_array latitudes_plus_corners;
    double *double_data;
    long i, dimensions[3];

    dimensions[0] = info->num_times;
    dimensions[1] = 1;
    dimensions[2] = LATLONG_DATA_INCL_CORNERS;
    CHECKED_MALLOC(latitudes_plus_corners.double_data, dimensions[0] * dimensions[1] * dimensions[2] * sizeof(double));
    if (read_variable(&info->data_cursor, "latitude_v8", 3, dimensions, -999.95, -999.25, latitudes_plus_corners) != 0)
    {
        free(latitudes_plus_corners.double_data);
        return -1;
    }

    double_data = data.double_data;
    for (i = 0; i < info->num_times; i++)
    {
        *double_data = latitudes_plus_corners.double_data[LATLONG_DATA_INCL_CORNERS * i + 1];
        double_data++;
        *double_data = latitudes_plus_corners.double_data[LATLONG_DATA_INCL_CORNERS * i + 2];
        double_data++;
        *double_data = latitudes_plus_corners.double_data[LATLONG_DATA_INCL_CORNERS * i + 3];
        double_data++;
        *double_data = latitudes_plus_corners.double_data[LATLONG_DATA_INCL_CORNERS * i + 4];
        double_data++;
    }
    return 0;
}

static int read_longitude_bounds(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;
    harp_array longitudes_plus_corners;
    double *double_data;
    long i, dimensions[3];

    dimensions[0] = info->num_times;
    dimensions[1] = 1;
    dimensions[2] = LATLONG_DATA_INCL_CORNERS;
    CHECKED_MALLOC(longitudes_plus_corners.double_data, dimensions[0] * dimensions[1] * dimensions[2] * sizeof(double));
    if (read_variable(&info->data_cursor, "longitude_v8", 3, dimensions, -999.95, -999.25, longitudes_plus_corners) !=
        0)
    {
        free(longitudes_plus_corners.double_data);
        return -1;
    }

    double_data = data.double_data;
    for (i = 0; i < info->num_times; i++)
    {
        *double_data = longitudes_plus_corners.double_data[LATLONG_DATA_INCL_CORNERS * i + 1];
        double_data++;
        *double_data = longitudes_plus_corners.double_data[LATLONG_DATA_INCL_CORNERS * i + 2];
        double_data++;
        *double_data = longitudes_plus_corners.double_data[LATLONG_DATA_INCL_CORNERS * i + 3];
        double_data++;
        *double_data = longitudes_plus_corners.double_data[LATLONG_DATA_INCL_CORNERS * i + 4];
        double_data++;
    }
    return 0;
}

static int read_geo_angle(const char *field_name, void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;
    harp_array angles_plus_filler;
    double *double_data;
    long i, dimensions[2];

    dimensions[0] = info->unused_geo_data_factor * info->num_times;
    dimensions[1] = info->unused_geo_data_factor;
    CHECKED_MALLOC(angles_plus_filler.double_data, dimensions[0] * dimensions[1] * sizeof(double));
    if (read_variable(&info->geo_cursor, field_name, 2, dimensions, -999.95, -999.25, angles_plus_filler) != 0)
    {
        free(angles_plus_filler.double_data);
        return -1;
    }

    double_data = data.double_data;
    for (i = 0; i < info->num_times; i++)
    {
        *double_data = angles_plus_filler.double_data[info->unused_geo_data_factor * info->unused_geo_data_factor * i];
        double_data++;
    }

    free(angles_plus_filler.double_data);
    return 0;
}

static int read_sensor_azimuth_angle(void *user_data, harp_array data)
{
    return read_geo_angle("SatelliteAzimuthAngle", user_data, data);
}

static int read_sensor_zenith_angle(void *user_data, harp_array data)
{
    return read_geo_angle("SatelliteZenithAngle", user_data, data);
}

static int read_solar_azimuth_angle(void *user_data, harp_array data)
{
    return read_geo_angle("SolarAzimuthAngle", user_data, data);
}

static int read_solar_zenith_angle(void *user_data, harp_array data)
{
    return read_geo_angle("SolarZenithAngle", user_data, data);
}

static int read_ozone_volume_mixing_ratio(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;
    long dimensions[3];

    dimensions[0] = info->num_times;
    dimensions[1] = 1;
    dimensions[2] = info->num_pressures;
    return read_variable(&info->data_cursor, "mixing_ratio_v8", 3, dimensions, -999.95, -999.25, data);
}

static int read_ozone_volume_mixing_ratio_pressure(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;
    harp_array mixing_pressures;
    long dimensions[3], i, j;

    dimensions[0] = info->num_times;
    dimensions[1] = 1;
    dimensions[2] = info->num_pressures;
    CHECKED_MALLOC(mixing_pressures.double_data, dimensions[0] * dimensions[1] * dimensions[2] * sizeof(double));
    if (read_variable(&info->data_cursor, "mixing_ratio_press_v8", 3, dimensions, -999.95, -999.25, mixing_pressures) !=
        0)
    {
        free(mixing_pressures.double_data);
        return -1;
    }
    for (i = 0; i < info->num_pressures; i++)
    {
        for (j = 0; j < info->num_times; j++)
        {
            if (!coda_isNaN(mixing_pressures.double_data[i + j * info->num_pressures]))
            {
                data.double_data[i] = mixing_pressures.double_data[i + j * info->num_pressures];
                break;
            }
        }
    }
    free(mixing_pressures.double_data);
    return 0;
}

static int init_cursors(ingest_info *info)
{
    const char *swath_name;
    coda_cursor cursor;
    coda_type *type;
    long num_swaths, swath_index;

    if (coda_cursor_set_product(&cursor, info->product) != 0)
    {
        harp_set_error(HARP_ERROR_CODA, NULL);
        return -1;
    }
    if (coda_cursor_goto_record_field_by_name(&cursor, "All_Data") != 0)
    {
        harp_set_error(HARP_ERROR_CODA, NULL);
        return -1;
    }
    if (coda_cursor_get_type(&cursor, &type) != 0)
    {
        harp_set_error(HARP_ERROR_CODA, NULL);
        return -1;
    }
    if (coda_type_get_num_record_fields(type, &num_swaths) != 0)
    {
        harp_set_error(HARP_ERROR_CODA, NULL);
        return -1;
    }
    for (swath_index = 0; swath_index < num_swaths; swath_index++)
    {
        if (coda_type_get_record_field_name(type, swath_index, &swath_name) != 0)
        {
            harp_set_error(HARP_ERROR_CODA, NULL);
            return -1;
        }
        if (coda_cursor_goto_record_field_by_index(&cursor, swath_index) != 0)
        {
            harp_set_error(HARP_ERROR_CODA, NULL);
            return -1;
        }
        if (strcmp(swath_name + strlen(swath_name) - 7, "GEO_All") == 0)
        {
            info->geo_cursor = cursor;
        }
        if ((strcmp(swath_name + strlen(swath_name) - 6, "IP_All") == 0) ||
            (strcmp(swath_name + strlen(swath_name) - 7, "EDR_All") == 0))
        {
            info->data_cursor = cursor;
        }
        if (coda_cursor_goto_parent(&cursor) != 0)
        {
            harp_set_error(HARP_ERROR_CODA, NULL);
            return -1;
        }
    }

    return 0;
}

static int get_dimensions(ingest_info *info)
{
    coda_cursor cursor;
    long coda_dimension[CODA_MAX_NUM_DIMS];
    long num_geo_times;
    int num_coda_dimensions;

    cursor = info->geo_cursor;
    if (coda_cursor_goto_record_field_by_name(&cursor, "MidTime") != 0)
    {
        harp_set_error(HARP_ERROR_CODA, NULL);
        return -1;
    }
    if (coda_cursor_get_array_dim(&cursor, &num_coda_dimensions, coda_dimension) != 0)
    {
        harp_set_error(HARP_ERROR_CODA, NULL);
        return -1;
    }
    num_geo_times = coda_dimension[0];

    cursor = info->data_cursor;
    if (coda_cursor_goto_record_field_by_name(&cursor, "mixing_ratio_v8") != 0)
    {
        harp_set_error(HARP_ERROR_CODA, NULL);
        return -1;
    }
    if (coda_cursor_get_array_dim(&cursor, &num_coda_dimensions, coda_dimension) != 0)
    {
        harp_set_error(HARP_ERROR_CODA, NULL);
        return -1;
    }
    info->num_times = coda_dimension[0];
    info->num_pressures = coda_dimension[2];
    info->unused_geo_data_factor = num_geo_times / info->num_times;

    return 0;
}

static int ingestion_init(const harp_ingestion_module *module, coda_product *product,
                          const harp_ingestion_options *options, harp_product_definition **definition, void **user_data)
{
    ingest_info *info;

    (void)options;

    CHECKED_MALLOC(info, sizeof(ingest_info));
    memset(info, '\0', sizeof(ingest_info));
    info->product = product;

    if (init_cursors(info) != 0)
    {
        ingestion_done(info);
        return -1;
    }
    if (get_dimensions(info) != 0)
    {
        ingestion_done(info);
        return -1;
    }

    *definition = *module->product_definition;
    *user_data = info;

    nan = coda_NaN();

    return 0;
}

/* Register the Nadir Profile Ozone in the OMPS IP and OMPS EDR files */
static void register_omps_profile_product(harp_ingestion_module *module, const char *swath_part,
                                          const char *productname)
{
    harp_product_definition *product_definition;
    harp_variable_definition *variable_definition;
    harp_dimension_type dimension_type[2] = { harp_dimension_time, harp_dimension_vertical };
    harp_dimension_type bounds_dimension_type[2] = { harp_dimension_time, harp_dimension_independent };
    long bounds_dimension[2] = { -1, 4 };
    const char *description;
    char path[255];

    product_definition = harp_ingestion_register_product(module, productname, NULL, read_dimensions);

    /* datetime */
    description = "time of the measurement";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "datetime", harp_type_double, 1, dimension_type,
                                                   NULL, description, "seconds since 2000-01-01", NULL, read_datetime);
    strcpy(path, "/All_Data/OMPS_NP_GEO_All/MidTime");
    description = "the time converted from seconds since 1958-01-01 to seconds since 2000-01-01T00:00:00";
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, description);

    /* latitude */
    description = "latitude";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "latitude", harp_type_double, 1, dimension_type,
                                                   NULL, description, "degree_north", NULL, read_latitude);
    harp_variable_definition_set_valid_range_double(variable_definition, -90.0, 90.0);
    sprintf(path, "/All_Data/OMPS_NP_%s_All/latitude_v8", swath_part);
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);

    /* longitude */
    description = "longitude";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "longitude", harp_type_double, 1, dimension_type,
                                                   NULL, description, "degree_east", NULL, read_longitude);
    harp_variable_definition_set_valid_range_double(variable_definition, -180.0, 180.0);
    sprintf(path, "/All_Data/OMPS_NP_%s_ALL/longitude_v8", swath_part);
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);

    /* latitude_bounds */
    description = "latitude corners";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "latitude_bounds", harp_type_double, 2,
                                                   bounds_dimension_type, bounds_dimension, description, "degree_north",
                                                   NULL, read_latitude_bounds);
    harp_variable_definition_set_valid_range_double(variable_definition, -90.0, 90.0);
    sprintf(path, "/All_Data/OMPS_NP_%s_All/latitude_v8", swath_part);
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);

    /* longitude_bounds */
    description = "longitude corners";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "longitude_bounds", harp_type_double, 2,
                                                   bounds_dimension_type, bounds_dimension, description, "degree_east",
                                                   NULL, read_longitude_bounds);
    harp_variable_definition_set_valid_range_double(variable_definition, -180.0, 180.0);
    sprintf(path, "/All_Data/OMPS_NP_%s_ALL/longitude_v8", swath_part);

    /* sensor_azimuth_angle */
    description = "azimuth angle (measured clockwise positive from North) to Satellite at each retrieval position";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "sensor_azimuth_angle",
                                                   harp_type_double, 1, dimension_type, NULL, description, "degree",
                                                   NULL, read_sensor_azimuth_angle);
    harp_variable_definition_set_valid_range_double(variable_definition, 0.0, 180.0);
    strcpy(path, "/All_Data/OMPS_NP_GEO_All/SatelliteAzimuthAngle");
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);

    /* sensor_zenith_angle */
    description = "zenith angle to Satellite at each retrieval position";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "sensor_zenith_angle",
                                                   harp_type_double, 1, dimension_type, NULL, description, "degree",
                                                   NULL, read_sensor_zenith_angle);
    harp_variable_definition_set_valid_range_double(variable_definition, 0.0, 180.0);
    strcpy(path, "/All_Data/OMPS_NP_GEO_All/SatelliteZenithAngle");
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);

    /* solar_azimuth_angle */
    description = "azimuth angle of sun (measured clockwise positive from North) at each retrieval position";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "solar_azimuth_angle",
                                                   harp_type_double, 1, dimension_type, NULL, description, "degree",
                                                   NULL, read_solar_azimuth_angle);
    harp_variable_definition_set_valid_range_double(variable_definition, 0.0, 180.0);
    strcpy(path, "/All_Data/OMPS_NP_GEO_All/SatelliteAzimuthAngle");
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);

    /* solar_zenith_angle */
    description = "zenith angle of sun at each retrieval position";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "solar_zenith_angle",
                                                   harp_type_double, 1, dimension_type, NULL, description, "degree",
                                                   NULL, read_solar_zenith_angle);
    harp_variable_definition_set_valid_range_double(variable_definition, 0.0, 180.0);
    strcpy(path, "/All_Data/OMPS_NP_GEO_All/SatelliteZenithAngle");
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);

    /* O3_volume_mixing_ratio */
    description = "ozone volume mixing ratio";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "O3_volume_mixing_ratio",
                                                   harp_type_double, 2, dimension_type, NULL, description, "ppmv",
                                                   NULL, read_ozone_volume_mixing_ratio);
    sprintf(path, "/All_Data/OMPS_NP_%s_All/mixing_ratio_v8", swath_part);
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);

    /* pressures during which O3_volume_mixing_ratio were measured */
    description = "pressure";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "pressure",
                                                   harp_type_double, 1, &(dimension_type[1]), NULL, description, "hPa",
                                                   NULL, read_ozone_volume_mixing_ratio_pressure);
    sprintf(path, "/All_Data/OMPS_NP_%s_All/mixing_ratio_press_v8", swath_part);
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);
}

/* This module ingests the OMPS IP and OMPS EDR files */
int harp_ingestion_module_npp_suomi_omps_profiles_l2_init(void)
{
    harp_ingestion_module *module;

    /* Ingestion of Nadir Profile Ozone IP (product type IMOP) */
    module = harp_ingestion_register_module("NPP_SUOMI_L2_OMPS_IP_IMOP", "NPP", "NPP_SUOMI", "OMPS_IP_IMOP_L2",
                                            "NPP Suomi OMPS IP Nadir Profile Ozone", ingestion_init, ingestion_done);
    register_omps_profile_product(module, "IP", "NPP_SUOMI_L2_OMPS_IP_IMOP");

    /* Ingestion of Nadir Profile Ozone (product type OONP) */
    module = harp_ingestion_register_module("NPP_SUOMI_L2_OMPS_EDR_OONP", "NPP", "NPP_SUOMI", "OMPS_EDR_OONP_L2",
                                            "NPP Suomi OMPS EDR Nadir Profile Ozone", ingestion_init, ingestion_done);
    register_omps_profile_product(module, "EDR", "NPP_SUOMI_L2_OMPS_EDR_OONP");

    /* The INPAK data has its geo-location in another file so it can not be ingested in HARP */

    return 0;
}

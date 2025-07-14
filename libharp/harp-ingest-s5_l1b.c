#include "coda.h"
#include "harp-ingestion.h"

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* Default fill value taken from "Input/output data specification for the TROPOMI L-1b data processor",
 * S5P-KNMI-L01B-0012-SD.
 */
#define DEFAULT_FILL_VALUE_INT (-2147483647)

/* Macro to determine the number of elements in a one dimensional C array. */
#define ARRAY_SIZE(X) (sizeof((X))/sizeof((X)[0]))

/* Maximum length of a path string in generated mapping descriptions. */
#define MAX_PATH_LENGTH 256

typedef enum s5_product_type_enum
{
    //s5_type_aui,
    //s5_type_ch4,
    //s5_type_no2,
    //s5_type_o3,
    //s5_type_so2,
    s5_type_nir,
    s5_type_irr,
} s5_product_type;

#define S5_NUM_PRODUCT_TYPES (((int)s5_type_irr) + 1)


typedef enum s5_dimension_type_enum
{
    //s5_dim_time = 0,    /* flattened scanline x pixel grid */
    s5_dim_scanline,    /* original along-track dimension */
    s5_dim_pixel,       /* original across-track dimension */
    s5_dim_corner,      /* 4 polygon corners per ground pixel */
    //s5_dim_layer,       /* pressure / altitude layers */
    //s5_dim_level,       /* layer +1 (bounds) */
    s5_dim_spectral,    /* extra wavelengths (e.g. spectral_channel) */
    //s5_dim_profile      /* short profile axis (SO2 options, etc.) */
} s5_dimension_type;

/* handy constant: last enum value + 1 */
#define S5_NUM_DIM_TYPES   ((int)s5_dim_spectral + 1)

// Dimensions of the original (and end) product. time is needed otherwise it will crush 
static const char *s5_dimension_name[S5_NUM_PRODUCT_TYPES][S5_NUM_DIM_TYPES] = {
    //{"time", "scanline", "ground_pixel", "pixel_corners", "spectral_channel", NULL, NULL, NULL},     /* NIR */
    {"scanline", "ground_pixel", "pixel_corners", "spectral_channel"},     /* NIR */
    //{"time", "scanline", "ground_pixel", "pixel_corners", "spectral_channel", NULL, NULL, NULL},     /* IRR */
    {"scanline", "ground_pixel", "pixel_corners", "spectral_channel"}//NULL, NULL, NULL},     /* IRR */
    //{"time", "scanline", "ground_pixel", "corner", "layer", NULL, NULL, NULL},  /* NO2 */
    //{"time", "scanline", "ground_pixel", "corner", "layer", NULL, NULL, NULL},  /* O3_ */
    //{"time", "scanline", "ground_pixel", "corner", "layer", NULL, NULL, "profile"},     /* SO2 */
    //{"time", "scanline", "ground_pixel", "corner", NULL, NULL, NULL, NULL},     /* CLD */
    //{"time", "scanline", "ground_pixel", "corner", "layer", NULL, NULL, NULL},  /* CO_ */
};

/* the array shape of delta_time variable for each data product */
static const int s5_delta_time_num_dims[S5_NUM_PRODUCT_TYPES] = { 1, 1, 1, 1, 1, 1 };

typedef struct ingest_info_struct
{
    coda_product *product;
    //int band;

    coda_cursor product_cursor    ; /* /data/band... */
    coda_cursor geolocation_cursor; /* /data/band.../geolocation_data */
    coda_cursor instrument_cursor ;
    coda_cursor observation_cursor;
    //long num_scanlines;
    //long num_pixels;
    //long num_channels;

    coda_cursor sensor_mode_cursor;
    coda_cursor geo_data_cursor;
    //coda_cursor observation_cursor;
    //coda_cursor instrument_cursor;

    //coda_cursor wavelength_cursor;
    //harp_scalar wavelength_fill_value;
    //coda_cursor observable_cursor;
    //harp_scalar observable_fill_value;
    //coda_cursor observable_error_cursor;
    //harp_scalar observable_error_fill_value;
    //coda_cursor observable_noise_cursor;
    //harp_scalar observable_noise_fill_value;

    //float *observable_buffer;   /* [num_channels] */


    // TODO: Fix these to the correct values according to the end product  
    //int use_co_corrected;
    //int use_co_nd_avk;
    //int use_ch4_band_options;   /* CH4: SWIR-1 (default), SWIR-3, or NIR-2 */
    //int use_cld_band_options;   /* CLD: BAND3A (default), or BAND3C */
    //int so2_column_type;        /* 0: PBL (anthropogenic), 1: 1km box profile, 2: 7km bp, 3: 15km bp, 4: layer height */

    int use_band_option; 

    s5_product_type product_type;
    long num_times;
    long num_scanlines;
    long num_pixels;
    long num_corners;
    long num_layers;
    long num_levels;
    long num_latitudes;
    long num_longitudes;
    long num_spectral;
    long num_profile;


    /* CLD */
    //coda_cursor b3a_product_cursor;
    //coda_cursor b3a_geolocation_cursor;
    //coda_cursor b3a_detailed_results_cursor;
    //coda_cursor b3a_input_data_cursor;
    //coda_cursor b3c_product_cursor;
    //coda_cursor b3c_geolocation_cursor;
    //coda_cursor b3c_detailed_results_cursor;
    //coda_cursor b3c_input_data_cursor;

    int processor_version;
    int collection_number;
    int wavelength_ratio;
    int ch4_option;     /* CH4: physics (default) or precision */
    int no2_column_option;      /* NO2: total (default) or summed */
    int is_nrti;

    uint8_t *surface_layer_status;      /* used for O3; 0: use as-is, 1: remove */



} ingest_info;


/* The routines start here 
 */

static const char *get_product_type_name(s5_product_type product_type)
{
    switch (product_type)
    {
        case s5_type_nir:
            return "SN5_1B_NIR";
        case s5_type_irr:
            return "SN5_1B_IRR";
        //case s5_type_no2:
        //    return "SN5_02_NO2";
        //case s5_type_o3:
        //    return "SN5_02_O3_";
        //case s5_type_so2:
        //    return "SN5_02_SO2";
        //case s5_type_cld:
        //    return "SN5_02_CLD";
        //case s5_type_co:
        //    return "SN5_02_CO_";
    }

    assert(0);
    exit(1);
}

/* Tiny helper for get_product_type() */
static void dash_to_underscore(char *s)
{
    /* use size_t for byte offsets into the char array */
    size_t i;

    /* Changing '-' to '_' */
    for (i = 0; s[i] != '\0'; ++i)
    {
        if (s[i] == '-')
        {
            s[i] = '_';
        }
    }
}


static void broadcast_array_int8(long num_scanlines, long num_pixels, int8_t *data)
{
    long i;

    /* Repeat the value for each scanline for all pixels in that scanline. Iterate in reverse to avoid overwriting
     * scanline values.
     */
    for (i = num_scanlines - 1; i >= 0; i--)
    {
        long j;

        for (j = 0; j < num_pixels; j++)
        {
            data[i * num_pixels + j] = data[i];
        }
    }
}


static void broadcast_array_int16(long num_scanlines, long num_pixels, int16_t *data)
{
    long i;

    /* Repeat the value for each scanline for all pixels in that scanline. Iterate in reverse to avoid overwriting
     * scanline values.
     */
    for (i = num_scanlines - 1; i >= 0; i--)
    {
        long j;

        for (j = 0; j < num_pixels; j++)
        {
            data[i * num_pixels + j] = data[i];
        }
    }
}

static void broadcast_array_int32(long num_scanlines, long num_pixels, int32_t *data)
{
    long i;

    /* Repeat the value for each scanline for all pixels in that scanline. Iterate in reverse to avoid overwriting
     * scanline values.
     */
    for (i = num_scanlines - 1; i >= 0; i--)
    {
        long j;

        for (j = 0; j < num_pixels; j++)
        {
            data[i * num_pixels + j] = data[i];
        }
    }
}


static void broadcast_array_float(long num_scanlines, long num_pixels, float *data)
{
    long i;

    /* Repeat the value for each scanline for all pixels in that scanline. Iterate in reverse to avoid overwriting
     * scanline values.
     */
    for (i = num_scanlines - 1; i >= 0; i--)
    {
        long j;

        for (j = 0; j < num_pixels; j++)
        {
            data[i * num_pixels + j] = data[i];
        }
    }
}

static void broadcast_array_double(long num_scanlines, long num_pixels, double *data)
{
    long i;

    /* Repeat the value for each scanline for all pixels in that scanline. Iterate in reverse to avoid overwriting
     * scanline values.
     */
    for (i = num_scanlines - 1; i >= 0; i--)
    {
        long j;

        for (j = 0; j < num_pixels; j++)
        {
            data[i * num_pixels + j] = data[i];
        }
    }
}

static int get_product_type(coda_product *product, s5_product_type *product_type)
{
    coda_cursor cursor, child, *src = NULL;
    char buf[256];      /* plenty of room for long IDs   */
    long len;
    int i;

    /* 1. bind root */
    if (coda_cursor_set_product(&cursor, product) != 0)
    {
        return harp_set_error(HARP_ERROR_CODA, NULL), -1;
    }

    /* 2. first try the clean ProductShortName */
    if (coda_cursor_goto(&cursor, "/METADATA/GRANULE_DESCRIPTION@ProductShortName") == 0)
    {
        src = &cursor;
    }
    else if (coda_cursor_goto(&cursor, "/@product_name") == 0)
    {
        /* may be scalar or 1-D array */
        coda_type_class tc;

        if (coda_cursor_get_type_class(&cursor, &tc) != 0)
        {
            return harp_set_error(HARP_ERROR_CODA, NULL), -1;
        }

        if (tc == coda_array_class)
        {
            child = cursor;
            if (coda_cursor_goto_first_array_element(&child) != 0)
            {
                return harp_set_error(HARP_ERROR_CODA, NULL), -1;
            }
            src = &child;
        }
        else
        {
            src = &cursor;
        }
    }
    else
    {
        return harp_set_error(HARP_ERROR_INGESTION, "cannot find product identifier"), -1;
    }

    /* 3. read the string */
    if (coda_cursor_get_string_length(src, &len) != 0 ||
        len <= 0 || len >= (long)sizeof(buf) || coda_cursor_read_string(src, buf, sizeof(buf)) != 0)
    {
        return harp_set_error(HARP_ERROR_CODA, NULL), -1;
    }

    /* 4. normalise and show */
    dash_to_underscore(buf);

    /* 5. search for any known short code */
    for (i = 0; i < S5_NUM_PRODUCT_TYPES; i++)
    {
        const char *code = get_product_type_name((s5_product_type)i);   /* e.g. "SN5_1B_NIR" */

        if (strstr(buf, code) != NULL)
        {
            *product_type = (s5_product_type)i;
            return 0;
        }
    }

    return harp_set_error(HARP_ERROR_INGESTION, "unsupported product type '%s'", buf), -1;
}


/* Recursively search for the named 1D dimension field within a CODA structure. */
static int find_dimension_length_recursive(coda_cursor *cursor, const char *name, long *length)
{
    coda_type_class type_class;

    if (coda_cursor_get_type_class(cursor, &type_class) != 0)
    {
        harp_set_error(HARP_ERROR_CODA, "Failed to get type class");
        return -1;
    }

    if (type_class == coda_record_class)
    {
        coda_cursor sub_cursor = *cursor;

        /* Navigate to the first field */
        if (coda_cursor_goto_first_record_field(&sub_cursor) == 0)
        {
            do
            {
                /* Attempt to navigate to the field by name */
                coda_cursor test_cursor = *cursor;

                if (coda_cursor_goto_record_field_by_name(&test_cursor, name) == 0)
                {
                    long coda_dim[CODA_MAX_NUM_DIMS];
                    int num_dims;

                    if (coda_cursor_get_array_dim(&test_cursor, &num_dims, coda_dim) != 0)
                    {
                        harp_set_error(HARP_ERROR_CODA, "Failed to get array dimensions");
                        return -1;
                    }

                    if (num_dims != 1)
                    {
                        harp_set_error(HARP_ERROR_INGESTION, "Field '%s' is not a 1D array", name);
                        return -1;
                    }

                    *length = coda_dim[0];
                    return 0;
                }

                /* Recursively search in the substructure */
                if (find_dimension_length_recursive(&sub_cursor, name, length) == 0)
                {
                    return 0;
                }

            } while (coda_cursor_goto_next_record_field(&sub_cursor) == 0);
        }
    }
    else if (type_class == coda_array_class)
    {
        long num_elements;

        if (coda_cursor_get_num_elements(cursor, &num_elements) != 0)
        {
            harp_set_error(HARP_ERROR_CODA, "Failed to get number of array elements");
            return -1;
        }

        if (num_elements > 0)
        {
            coda_cursor sub_cursor = *cursor;

            if (coda_cursor_goto_array_element_by_index(&sub_cursor, 0) != 0)
            {
                harp_set_error(HARP_ERROR_CODA, "Failed to go to array element");
                return -1;
            }

            if (find_dimension_length_recursive(&sub_cursor, name, length) == 0)
            {
                return 0;
            }
        }
    }

    /* Not found in this branch */
    return -1;
}

/* Find dimension length by recursively searching under data/PRODUCT. */
static int get_dimension_length(ingest_info *info, const char *name, long *length)
{
    coda_cursor cursor = info->product_cursor;

    if (find_dimension_length_recursive(&cursor, name, length) != 0)
    {
        harp_set_error(HARP_ERROR_INGESTION, "Dimension '%s' not found in product structure", name);
        return -1;
    }

    return 0;
}


/* Init Routines */

/* Initialize CODA cursors for main record groups with inline comments. */
static int init_cursors(ingest_info *info)
{
    coda_cursor cursor;
    char* curr_band; 

    /* Choosing the apropriate dataset based on the option chosen */
    if (info->product_type == s5_type_nir)
    {
        if (info->use_band_option == 0)
	{
            printf("[init_cursors]: band=3a\n"); 
	    curr_band = "band3a"; 
	}
	else if (info->use_band_option == 1)
	{
            printf("[init_cursors]: band=3b\n"); 
	    curr_band = "band3b"; 
	}
	else if (info->use_band_option == 2)
	{
            printf("[init_cursors]: band=3c\n"); 
	    curr_band = "band3c"; 
	}
	else
	{
            printf("ERROR\n"); 
            harp_set_error(HARP_ERROR_CODA, NULL);
            return -1;
	}
    }
    //if (info->use_cld_band_options == 0)
    //{
    //    info->product_cursor = info->b3a_product_cursor;
    //    info->geolocation_cursor = info->b3a_geolocation_cursor;
    //    info->detailed_results_cursor = info->b3a_detailed_results_cursor;
    //    info->input_data_cursor = info->b3a_input_data_cursor;
    //}
    //else
    //{
    //    info->product_cursor = info->b3c_product_cursor;
    //    info->geolocation_cursor = info->b3c_geolocation_cursor;
    //    info->detailed_results_cursor = info->b3c_detailed_results_cursor;
    //    info->input_data_cursor = info->b3c_input_data_cursor;
    //}

    /* Bind a cursor to the root of the CODA product */
    if (coda_cursor_set_product(&cursor, info->product) != 0)
    {
        harp_set_error(HARP_ERROR_CODA, NULL);
        return -1;
    }

    // TODO: Maybe create an array of strings to loop through for each band
    /* Products has to set of bands each containing its own product type */
    if (coda_cursor_goto_record_field_by_name(&cursor, curr_band) != 0)
    {
        /* Fallback to data/band* for simulated files */
        if (coda_cursor_goto_record_field_by_name(&cursor, "data") != 0 ||
            coda_cursor_goto_record_field_by_name(&cursor, curr_band) != 0)
        {
            harp_set_error(HARP_ERROR_CODA, NULL);
            return -1;
        }
    }
    /* Save data/band* cursor; subsequent navigation is relative to this. */
    info->product_cursor = cursor;

    /* Enter SUPPORT_DATA under PRODUCT (same location for both layouts):
     * '/PRODUCT/SUPPORT_DATA' or '/data/PRODUCT/SUPPORT_DATA'
     */
    //if (coda_cursor_goto_record_field_by_name(&cursor, "SUPPORT_DATA") != 0)
    //{
    //    harp_set_error(HARP_ERROR_CODA, NULL);
    //    return -1;
    //}

    /* Geolocation group: under band*
     * '/data/band.../geolocation_data' for both layouts.
     */
    if (coda_cursor_goto_record_field_by_name(&cursor, "geolocation_data") != 0)
    {
        harp_set_error(HARP_ERROR_CODA, NULL);
        return -1;
    }
    info->geolocation_cursor = cursor;

    /* Back to data/band* */
    coda_cursor_goto_parent(&cursor);

    /* Instrument data: '/data/band.../instrument_data' */
    if (coda_cursor_goto_record_field_by_name(&cursor, "instrument_data") != 0)
    {
        harp_set_error(HARP_ERROR_CODA, NULL);
        return -1;
    }
    info->instrument_cursor = cursor;

    /* Back to data/band* */
    coda_cursor_goto_parent(&cursor);

    /* Observation data: '/data/band.../observation_data' */
    if (coda_cursor_goto_record_field_by_name(&cursor, "observation_data") != 0)
    {
        harp_set_error(HARP_ERROR_CODA, NULL);
        return -1;
    }
    info->observation_cursor = cursor;

    return 0;
}

// TODO: Correct this one; there is no levels/layers. Also need to add spectral dimension 
/* Initialize record dimension lengths for the Sentinel-5 simulated L1b dataset */
static int init_dimensions(ingest_info *info)
{
    /* Get number of scanlines */
    if (s5_dimension_name[info->product_type][s5_dim_scanline] != NULL)
    {
        if (get_dimension_length(info, s5_dimension_name[info->product_type][s5_dim_scanline],
                                 &info->num_scanlines) != 0)
        {
            return -1;
        }
    }

    /* Get number of ground pixels */
    if (s5_dimension_name[info->product_type][s5_dim_pixel] != NULL)
    {
        if (get_dimension_length(info, s5_dimension_name[info->product_type][s5_dim_pixel], &info->num_pixels) != 0)
        {
            return -1;
        }
    }

    /* Get number of corners and validate */
    if (s5_dimension_name[info->product_type][s5_dim_corner] != NULL)
    {
        if (get_dimension_length(info, s5_dimension_name[info->product_type][s5_dim_corner], &info->num_corners) != 0)
        {
            return -1;
        }
        if (info->num_corners != 4)
        {
            harp_set_error(HARP_ERROR_INGESTION, "dimension '%s' has length %ld; expected 4",
                           s5_dimension_name[info->product_type][s5_dim_corner], info->num_corners);
            return -1;
        }
    }

    /* Get number of spectral channels and validate */
    if (s5_dimension_name[info->product_type][s5_dim_spectral] != NULL)
    {
        if (get_dimension_length(info, s5_dimension_name[info->product_type][s5_dim_spectral], &info->num_spectral) != 0)
        {
            return -1;
        }
        if (info->num_corners != 4)
        {
            harp_set_error(HARP_ERROR_INGESTION, "dimension '%s' has length %ld; expected N",
                           s5_dimension_name[info->product_type][s5_dim_spectral], info->num_spectral);
            return -1;
        }
    }

    /* Get number of layers */
    //if (s5_dimension_name[info->product_type][s5_dim_layer] != NULL)
    //{
    //    if (get_dimension_length(info, s5_dimension_name[info->product_type][s5_dim_layer], &info->num_layers) != 0)
    //    {
    //        return -1;
    //    }
    //}

    //if (s5_dimension_name[info->product_type][s5_dim_level] != NULL)
    //{
    //    if (get_dimension_length(info, s5_dimension_name[info->product_type][s5_dim_level], &info->num_levels) != 0)
    //    {
    //        return -1;
    //    }
    //}

    ///* Infer levels = layers + 1 */
    //if (info->num_layers > 0 && info->num_levels > 0)
    //{
    //    if (info->num_levels != info->num_layers + 1)
    //    {
    //        harp_set_error(HARP_ERROR_INGESTION, "dimension '%s' has length %ld; expected %ld",
    //                       s5_dimension_name[info->product_type][s5_dim_level], info->num_levels, info->num_layers + 1);
    //        return -1;
    //    }
    //}
    //else if (info->num_layers > 0)
    //{
    //    info->num_levels = info->num_layers + 1;
    //}
    //else if (info->num_levels > 0)
    //{
    //    if (info->num_levels < 2)
    //    {
    //        harp_set_error(HARP_ERROR_INGESTION, "dimension '%s' has length %ld; expected >= 2",
    //                       s5_dimension_name[info->product_type][s5_dim_level], info->num_levels);
    //        return -1;
    //    }

    //    info->num_layers = info->num_levels - 1;
    //}

    return 0;
}


/* Extract Sentinel-5 L1b product collection and processor version
 * from the global "logical product name".
 */
static int init_versions(ingest_info *info)
{
    coda_cursor cursor;
    char product_name[84];

    /* Since earlier S5P L2 products did not always have a valid 'id' global attribute
     * we will keep the version numbers at -1 if we can't extract the right information.
     */
    if (coda_cursor_set_product(&cursor, info->product) != 0)
    {
        harp_set_error(HARP_ERROR_CODA, NULL);
        return -1;
    }
    if (coda_cursor_goto(&cursor, "/@id") != 0)
    {
        /* no global 'id' attribute */
        return 0;
    }
    if (coda_cursor_read_string(&cursor, product_name, 84) != 0)
    {
        harp_set_error(HARP_ERROR_CODA, NULL);
        return -1;
    }
    if (strlen(product_name) != 83)
    {
        /* 'id' attribute does not contain a valid logical product name */
        return 0;
    }

    /* Populating the variables */
    info->collection_number = (int)strtol(&product_name[58], NULL, 10);
    info->processor_version = (int)strtol(&product_name[61], NULL, 10);

    return 0;
}

static void ingestion_done(void *user_data)
{
    ingest_info *info = (ingest_info *)user_data;

    if (info->surface_layer_status != NULL)
    {
        free(info->surface_layer_status);
    }

    free(info);
}


static int ingestion_init(const harp_ingestion_module *module, coda_product *product,
                          const harp_ingestion_options *options, harp_product_definition **definition, void **user_data)
{
    const char *option_value;
    ingest_info *info;

    info = (ingest_info *)malloc(sizeof(ingest_info));

    if (info == NULL)
    {
        harp_set_error(HARP_ERROR_OUT_OF_MEMORY, "out of memory (could not allocate %lu bytes) (%s:%u)",
                       sizeof(ingest_info), __FILE__, __LINE__);
        return -1;
    }

    info->product = product;


    info->num_times = 0;
    info->num_scanlines = 0;
    info->num_pixels = 0;
    info->num_corners = 0;
    //info->num_layers = 0;
    //info->num_levels = 0;

    info->num_spectral = 0;
    //info->num_profile = 0;

    //info->wavelength_ratio = 354;

    //info->surface_layer_status = NULL;

    /* default */
    //info->ch4_option = 0;
    //info->use_ch4_band_options = 0;
    //info->no2_column_option = 0;
    //info->use_cld_band_options = 0;     /* CLD: BAND3A (default), or BAND3C */
    //info->so2_column_type = 0;  /* 0=PBL (default)  1=1 km  2=7 km  3=15 km */

    info->use_band_option = 0; /* Each product has its own band option */ 


    printf("[ingestion_init]: get_product_type\n"); 

    if (get_product_type(info->product, &info->product_type) != 0)
    {
        ingestion_done(info);
        return -1;
    }

    printf("[ingestion_init]: init_versions\n"); 

    if (init_versions(info) != 0)
    {
        ingestion_done(info);
        return -1;
    }

    printf("[ingestion_init]: defition\n"); 

    *definition = *module->product_definition;

    printf("[ingestion_init]: has_option\n"); 

    //if (harp_ingestion_options_has_option(options, "wavelength_ratio"))
    //{
    //    if (harp_ingestion_options_get_option(options, "wavelength_ratio", &option_value) != 0)
    //    {
    //        ingestion_done(info);
    //        return -1;
    //    }
    //    if (strcmp(option_value, "335_367nm") == 0)
    //    {
    //        info->wavelength_ratio = 335;
    //    }
    //    else if (strcmp(option_value, "354_388nm") == 0)
    //    {
    //        info->wavelength_ratio = 354;
    //    }
    //    else
    //    {
    //        /* Option values are guaranteed to be legal if present. */
    //        assert(strcmp(option_value, "340_380nm") == 0);
    //        info->wavelength_ratio = 340;
    //    }
    //}

    if (info->product_type == s5_type_nir)
    {
        if (harp_ingestion_options_has_option(options, "band"))
        {
            if (harp_ingestion_options_get_option(options, "band", &option_value) != 0)
            {
                ingestion_done(info);
                return -1;
            }
            if (strcmp(option_value, "3b") == 0)
            {
         	info->use_band_option = 1; 
                printf("[ingestion_init]: band=3b\n"); 
            }
            else if (strcmp(option_value, "3c") == 0)
            {
         	info->use_band_option = 2; 
                printf("[ingestion_init]: band=3c\n"); 
            }
            else
            {
                /* Option values are guaranteed to be legal if present. */
                assert(strcmp(option_value, "3a") == 0);
         	info->use_band_option = 0; 
                printf("[ingestion_init]: band=3a\n"); 
            }
        }
    }


    printf("[ingestion_init]: init_cursors\n"); 
    if (init_cursors(info) != 0)
    {
        ingestion_done(info);
        return -1;
    }


    /* Getting input product dimensios */
    if (init_dimensions(info) != 0)
    {
        ingestion_done(info);
        return -1;
    }


    printf("[ingestion_init]: num_scanlines = %d\n", info->num_scanlines); 
    printf("[ingestion_init]: num_pixels    = %d\n", info->num_pixels); 
    printf("[ingestion_init]: num_corners   = %d\n", info->num_corners); 
    printf("[ingestion_init]: num_spectral  = %d\n", info->num_spectral); 

    /* Adding spectral dimension depending on the L1b product */
    //if (info->product_type != s5_type_irr)
    //{
    //    info->num_spectral = 196; /* spectral_channel */
    //}
    //else
    //{
    //    info->num_spectral = 102; /*  spectral_channel  */
    //}

    *user_data = info;

    return 0;
}


/* Reading Routines */

/* Supply HARP with the lengths of the global axes for the
 * Sentinel-5 simulated products.  
 */
static int read_dimensions(void *user_data, long dimension[HARP_NUM_DIM_TYPES])
{
    ingest_info *info = (ingest_info *)user_data;

    /* From the online documentation: 
     *
     * time       : Temporal dimension; this is also the only appendable dimension.
     * vertical   : Vertical dimension, indicating height or depth.
     * spectral   : Spectral dimension, associated with wavelength, wavenumber, or frequency.
     * latitude   : Latitude dimension, only to be used for the latitude axis
     *              of a regular latitude x longitude grid.
     * longitude  : Longitude dimension, only to be used for the longitude axis
     *              of a regular latitude x longitude grid.
     * independent: Independent dimension, used to index other quantities, such
     *              as the corner coordinates of ground pixel polygons.
     *
     * [Note]: Within a HARP product, all dimensions of the same type should
     * have the same length, except independent dimensions. For example, it is
     * an error to have two variables within the same product that both have a
     * time dimension, yet of a different length.
     */


    dimension[harp_dimension_time]     = info->num_scanlines * info->num_pixels;
    dimension[harp_dimension_spectral] = info->num_spectral;

    /* 2. vertical grid - only if available */
    //if (info->num_layers > 0)
    //{
    //    dimension[harp_dimension_vertical] = info->num_layers;
    //}

    switch (info->product_type)
    {
        //case s5_type_aui:
        //    dimension[harp_dimension_spectral] = info->num_spectral;
        //    break;
        //case s5_type_ch4:
        //    dimension[harp_dimension_spectral] = info->num_spectral;
        //    break;
        //case s5_type_so2:
        //    dimension[harp_dimension_time] = info->num_scanlines * info->num_pixels;
        //    break;
        //    /* CLD, NO2, CO, ... need no extra axes */
        default:
            break;
    }

    return 0;
}

/* Copied from the s5p l2 module */
static int read_dataset(coda_cursor cursor, const char *dataset_name, harp_data_type data_type, long num_elements,
                        harp_array data)
{
    long coda_num_elements;
    harp_scalar fill_value;

    if (coda_cursor_goto_record_field_by_name(&cursor, dataset_name) != 0)
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
        harp_set_error(HARP_ERROR_INGESTION, "dataset has %ld elements; expected %ld", coda_num_elements, num_elements);
        harp_add_coda_cursor_path_to_error_message(&cursor);
        return -1;
    }

    switch (data_type)
    {
        case harp_type_int8:
            {
                coda_native_type read_type;

                if (coda_cursor_goto_first_array_element(&cursor) != 0)
                {
                    harp_set_error(HARP_ERROR_CODA, NULL);
                    return -1;
                }
                if (coda_cursor_get_read_type(&cursor, &read_type) != 0)
                {
                    harp_set_error(HARP_ERROR_CODA, NULL);
                    return -1;
                }
                coda_cursor_goto_parent(&cursor);
                if (read_type == coda_native_type_uint8)
                {
                    if (coda_cursor_read_uint8_array(&cursor, (uint8_t *)data.int8_data, coda_array_ordering_c) != 0)
                    {
                        harp_set_error(HARP_ERROR_CODA, NULL);
                        return -1;
                    }
                }
                else
                {
                    if (coda_cursor_read_int8_array(&cursor, data.int8_data, coda_array_ordering_c) != 0)
                    {
                        harp_set_error(HARP_ERROR_CODA, NULL);
                        return -1;
                    }
                }
            }
            break;
        case harp_type_int16:
            {
                coda_native_type read_type;

                if (coda_cursor_goto_first_array_element(&cursor) != 0)
                {
                    harp_set_error(HARP_ERROR_CODA, NULL);
                    return -1;
                }
                if (coda_cursor_get_read_type(&cursor, &read_type) != 0)
                {
                    harp_set_error(HARP_ERROR_CODA, NULL);
                    return -1;
                }
                coda_cursor_goto_parent(&cursor);
                if (read_type == coda_native_type_uint16)
                {
                    if (coda_cursor_read_uint16_array(&cursor, (uint16_t *)data.int16_data, coda_array_ordering_c) != 0)
                    {
                        harp_set_error(HARP_ERROR_CODA, NULL);
                        return -1;
                    }
                }
                else
                {
                    if (coda_cursor_read_int16_array(&cursor, data.int16_data, coda_array_ordering_c) != 0)
                    {
                        harp_set_error(HARP_ERROR_CODA, NULL);
                        return -1;
                    }
                }
            }
            break;
        case harp_type_int32:
            {
                coda_native_type read_type;

                if (coda_cursor_goto_first_array_element(&cursor) != 0)
                {
                    harp_set_error(HARP_ERROR_CODA, NULL);
                    return -1;
                }
                if (coda_cursor_get_read_type(&cursor, &read_type) != 0)
                {
                    harp_set_error(HARP_ERROR_CODA, NULL);
                    return -1;
                }
                coda_cursor_goto_parent(&cursor);
                if (read_type == coda_native_type_uint32)
                {
                    if (coda_cursor_read_uint32_array(&cursor, (uint32_t *)data.int32_data, coda_array_ordering_c) != 0)
                    {
                        harp_set_error(HARP_ERROR_CODA, NULL);
                        return -1;
                    }
                }
                else
                {
                    if (coda_cursor_read_int32_array(&cursor, data.int32_data, coda_array_ordering_c) != 0)
                    {
                        harp_set_error(HARP_ERROR_CODA, NULL);
                        return -1;
                    }
                }
            }
            break;
        case harp_type_float:
            if (coda_cursor_read_float_array(&cursor, data.float_data, coda_array_ordering_c) != 0)
            {
                harp_set_error(HARP_ERROR_CODA, NULL);
                return -1;
            }
            if (coda_cursor_goto(&cursor, "@FillValue[0]") != 0)
            {
                harp_set_error(HARP_ERROR_CODA, NULL);
                return -1;
            }
            if (coda_cursor_read_float(&cursor, &fill_value.float_data) != 0)
            {
                harp_set_error(HARP_ERROR_CODA, NULL);
                return -1;
            }
            /* Replace values equal to the _FillValue variable attribute by NaN. */
            harp_array_replace_fill_value(data_type, num_elements, data, fill_value);
            break;
        case harp_type_double:
            if (coda_cursor_read_double_array(&cursor, data.double_data, coda_array_ordering_c) != 0)
            {
                harp_set_error(HARP_ERROR_CODA, NULL);
                return -1;
            }
            if (coda_cursor_goto(&cursor, "@FillValue[0]") != 0)
            {
                harp_set_error(HARP_ERROR_CODA, NULL);
                return -1;
            }
            if (coda_cursor_read_double(&cursor, &fill_value.double_data) != 0)
            {
                harp_set_error(HARP_ERROR_CODA, NULL);
                return -1;
            }
            /* Replace values equal to the _FillValue variable attribute by NaN. */
            harp_array_replace_fill_value(data_type, num_elements, data, fill_value);
            break;
        default:
            assert(0);
            exit(1);
    }

    return 0;
}

/* Read and convert the observation time array for Sentinel-5 simulated L1b data */
static int read_datetime(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;
    harp_array time_reference_array;
    double time_reference;
    long i;

    /* 1) Read the single time reference value (seconds since 2010-01-01) */
    time_reference_array.ptr = &time_reference;
    if (read_dataset(info->observation_cursor, "time", harp_type_double, 1, time_reference_array) != 0)
    {
        return -1;
    }

    /* 2) Read delta_time and optionally broadcast:
     *    - If standard layout (2D), read num_scanlines values then broadcast over pixels.
     *    - If simulated layout (1D), read num_scanlines values only.
     */
    if (s5_delta_time_num_dims[info->product_type] == 2)
    {
        /* Standard S5P: one delta_time per scanline, then repeat for each pixel */
        if (read_dataset(info->observation_cursor, "delta_time", harp_type_double, info->num_scanlines, data) != 0)
        {
            return -1;
        }
        broadcast_array_double(info->num_scanlines, info->num_pixels, data.double_data);
    }
    else
    {
        /* Simulated: exactly one delta_time per scanline, no broadcast */
        if (read_dataset(info->observation_cursor, "delta_time", harp_type_double, info->num_scanlines, data) != 0)
        {
            return -1;
        }
    }

    /* 3) Convert milliseconds to seconds and add to reference time */
    {
        long count = info->num_scanlines * (s5_delta_time_num_dims[info->product_type] == 2 ? info->num_pixels : 1);

        for (i = 0; i < count; i++)
        {
            data.double_data[i] = time_reference + data.double_data[i] / 1e3;
        }
    }

    return 0;
}



/* Read the absolute orbit number from the global attribute */
static int read_orbit_index(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;
    coda_cursor cursor;
    coda_native_type read_type;
    uint32_t uval;
    int32_t ival;

    /* 1) Bind a cursor to the root product */
    if (coda_cursor_set_product(&cursor, info->product) != 0)
    {
        harp_set_error(HARP_ERROR_CODA, NULL);
        return -1;
    }

    /* 2) Try /@orbit_start first, then /@orbit */
    if (coda_cursor_goto(&cursor, "/@orbit_start") != 0 && coda_cursor_goto(&cursor, "/@orbit") != 0)
    {
        harp_set_error(HARP_ERROR_CODA, NULL);
        return -1;
    }

    /* 3) If it's an array, move to its first element */
    {
        coda_type_class tc;

        if (coda_cursor_get_type_class(&cursor, &tc) != 0)
        {
            return -1;
        }
        if (tc == coda_array_class)
        {
            if (coda_cursor_goto_first_array_element(&cursor) != 0)
            {
                harp_set_error(HARP_ERROR_CODA, NULL);
                return -1;
            }
        }
    }

    /* 4) Determine the native storage type and read appropriately */
    if (coda_cursor_get_read_type(&cursor, &read_type) != 0)
    {
        harp_set_error(HARP_ERROR_CODA, NULL);
        return -1;
    }
    if (read_type == coda_native_type_uint32)
    {
        /* Stored as an unsigned 32-bit */
        if (coda_cursor_read_uint32(&cursor, &uval) != 0)
        {
            harp_set_error(HARP_ERROR_CODA, NULL);
            return -1;
        }
        ival = (int32_t)uval;
    }
    else
    {
        /* Stored as a signed 32-bit (or other compatible) */
        if (coda_cursor_read_int32(&cursor, &ival) != 0)
        {
            harp_set_error(HARP_ERROR_CODA, NULL);
            return -1;
        }
    }

    /* 5) Write back into the HARP buffer */
    data.int32_data[0] = ival;
    return 0;
}

/* Field: data/band.../geolocation_data */

static int read_geolocation_latitude(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    return read_dataset(info->geolocation_cursor, "latitude", harp_type_float, info->num_scanlines * info->num_pixels,
                        data);
}

static int read_geolocation_longitude(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    return read_dataset(info->geolocation_cursor, "longitude", harp_type_float, info->num_scanlines * info->num_pixels,
                        data);
}

static int read_geolocation_latitude_bounds(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    return read_dataset(info->geolocation_cursor, "latitude_bounds", harp_type_float,
                        info->num_scanlines * info->num_pixels * info->num_corners, data);
}

static int read_geolocation_longitude_bounds(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    return read_dataset(info->geolocation_cursor, "longitude_bounds", harp_type_float,
                        info->num_scanlines * info->num_pixels * info->num_corners, data);
}

static int read_geolocation_satellite_altitude(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    if (read_dataset(info->geolocation_cursor, "satellite_altitude", harp_type_int32, info->num_scanlines, data) != 0)
    {
        return -1;
    }

    broadcast_array_int32(info->num_scanlines, info->num_pixels, data.int32_data);

    return 0;
}

static int read_geolocation_satellite_latitude(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    if (read_dataset(info->geolocation_cursor, "satellite_latitude", harp_type_float, info->num_scanlines, data) != 0)
    {
        return -1;
    }

    broadcast_array_float(info->num_scanlines, info->num_pixels, data.float_data);

    return 0;
}


static int read_geolocation_satellite_longitude(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    if (read_dataset(info->geolocation_cursor, "satellite_longitude", harp_type_float, info->num_scanlines, data) != 0)
    {
        return -1;
    }

    broadcast_array_float(info->num_scanlines, info->num_pixels, data.float_data);

    return 0;
}

static int read_geolocation_satellite_orbit_phase(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    if (read_dataset(info->geolocation_cursor, "satellite_orbit_phase", harp_type_float, info->num_scanlines, data) != 0)
    {
        return -1;
    }

    broadcast_array_float(info->num_scanlines, info->num_pixels, data.float_data);

    return 0;
}

static int read_geolocation_solar_zenith_angle(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    return read_dataset(info->geolocation_cursor, "solar_zenith_angle", harp_type_float,
                        info->num_scanlines * info->num_pixels, data);
}

static int read_geolocation_solar_azimuth_angle(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    return read_dataset(info->geolocation_cursor, "solar_azimuth_angle", harp_type_float,
                        info->num_scanlines * info->num_pixels, data);
}

static int read_geolocation_viewing_azimuth_angle(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    return read_dataset(info->geolocation_cursor, "viewing_azimuth_angle", harp_type_float,
                        info->num_scanlines * info->num_pixels, data);
}


static int read_geolocation_viewing_zenith_angle(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    return read_dataset(info->geolocation_cursor, "viewing_zenith_angle", harp_type_float,
                        info->num_scanlines * info->num_pixels, data);
}



/* Observation variables */

static int read_observation_measurement_quality(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    if (read_dataset(info->observation_cursor, "measurement_quality", harp_type_int16, info->num_scanlines, data) != 0)
    {
        return -1;
    }

    broadcast_array_int16(info->num_scanlines, info->num_pixels, data.int16_data);

    return 0;
}


static int read_observation_radiance(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    if (read_dataset(info->observation_cursor, "radiance", harp_type_float,
                     info->num_scanlines * info->num_pixels * info->num_spectral, data) != 0)
    {
        return -1;
    }

    return 0;
}

static int read_observation_radiance_error(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    if (read_dataset(info->observation_cursor, "radiance_error", harp_type_int8,
                     info->num_scanlines * info->num_pixels * info->num_spectral, data) != 0)
    {
        return -1;
    }

    broadcast_array_int16(info->num_scanlines, info->num_pixels, data.int8_data);

    return 0;
}

static int read_observation_radiance_noise(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    if (read_dataset(info->observation_cursor, "radiance_noise", harp_type_int8,
                     info->num_scanlines * info->num_pixels * info->num_spectral, data) != 0)
    {
        return -1;
    }

    broadcast_array_int16(info->num_scanlines, info->num_pixels, data.int8_data);

    return 0;
}

static int read_observation_spectral_channel_quality(void *user_data, harp_array data)
{
    ingest_info *info = (ingest_info *)user_data;

    if (read_dataset(info->observation_cursor, "spectral_channel_quality", harp_type_int8,
                     info->num_scanlines * info->num_pixels * info->num_spectral, data) != 0)
    {
        return -1;
    }

    broadcast_array_int16(info->num_scanlines, info->num_pixels, data.int8_data);

    return 0;
}




/* 
 * Products' Registration Routines 
 */

static void register_mapping_per_band(const char *product_type,
		harp_variable_definition *variable_definition, 
		const char* variable_name, const char* dataset_name,  
		const char* bands_list[], int num_bands)
{
    //const char *path;
    int i; 
    char path[MAX_PATH_LENGTH];

    // Loop through array of strings
    //for (int i = 0; i < num_bands; i++) 
    //{
    //    printf("Band %d: %s\n", i, bands_list[i]);
    //}

    //if (strcmp(product_type, "SN5_1B_NIR") == 0)
    //{
    //path = "/data/band3a/geolocation_data/latitude[]";
    //harp_variable_definition_add_mapping(variable_definition, "band=3a or band unset", NULL, path, NULL);
    //path = "/data/band3b/geolocation_data/latitude[]";
    //harp_variable_definition_add_mapping(variable_definition, "band=3b", NULL, path, NULL);
    //path = "/data/band3c/geolocation_data/latitude[]";
    //harp_variable_definition_add_mapping(variable_definition, "band=3c", NULL, path, NULL);
    //}

    for (i = 0; i < num_bands; i++) 
    {
        printf("Band %d: %s\n", i, bands_list[i], dataset_name, variable_name);
        snprintf(path, MAX_PATH_LENGTH, "/data/%s/%s/%s", bands_list[i], dataset_name, variable_name);
        harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);
    }
}

static void register_geolocation_variables(harp_product_definition
		*product_definition, const char *product_type, const char* bands_list[], int num_bands)
{
    const char *path;
    const char *description;

    harp_variable_definition *variable_definition;

    harp_dimension_type dimension_type_1d[1] = { harp_dimension_time };
    harp_dimension_type dimension_type_2d[2] = { harp_dimension_time, harp_dimension_independent };
    harp_dimension_type dimension_type_2d_spec[2] = { harp_dimension_time, harp_dimension_spectral };
    long bounds_dimension[2] = { -1, 4 };

    //const char* bands_list[] = {"band=3a or band unset", "band=3b", "band=3c"};
    //int num_bands = sizeof(bands_list) / sizeof(bands_list[0]);


    /* latitude */
    description = "Latitude of the center of each ground pixel on the WGS84 reference ellipsoid.";
    variable_definition =
	harp_ingestion_register_variable_full_read(product_definition,
			"latitude", harp_type_float, 1, dimension_type_1d,
			NULL, description, "degree_north", NULL,
			read_geolocation_latitude);
    harp_variable_definition_set_valid_range_float(variable_definition, -90.0f, 90.0f);

    //if (strcmp(product_type, "SN5_1B_NIR") == 0)
    //{
    //    path = "/data/band3a/geolocation_data/latitude[]";
    //    harp_variable_definition_add_mapping(variable_definition, "band=3a or band unset", NULL, path, NULL);
    //    path = "/data/band3b/geolocation_data/latitude[]";
    //    harp_variable_definition_add_mapping(variable_definition, "band=3b", NULL, path, NULL);
    //    path = "/data/band3c/geolocation_data/latitude[]";
    //    harp_variable_definition_add_mapping(variable_definition, "band=3c", NULL, path, NULL);
    //}
    register_mapping_per_band(product_type, variable_definition, "latitude[]", "geolocation_data", bands_list, num_bands); 

    /* longitude */
    description = "Longitude of the center of each ground pixel on the WGS84 reference ellipsoid.";
    variable_definition =
	harp_ingestion_register_variable_full_read(product_definition,
			"longitude", harp_type_float, 1, dimension_type_1d,
			NULL, description, "degree_east", NULL,
			read_geolocation_longitude);
    if (strcmp(product_type, "SN5_1B_NIR") == 0)
    {
        harp_variable_definition_set_valid_range_float(variable_definition, -180.0f, 180.0f);
        path = "/data/band3a/geolocation_data/longitude[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3a or band unset", NULL, path, NULL);
        path = "/data/band3b/geolocation_data/longitude[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3b", NULL, path, NULL);
        path = "/data/band3c/geolocation_data/longitude[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3c", NULL, path, NULL);
    }


    /* latitude_bounds */
    description = "The four latitude boundaries of each ground pixel on the WGS84 reference ellipsoid.";
    variable_definition =
	harp_ingestion_register_variable_full_read(product_definition,
			"latitude_bounds", harp_type_float, 2,
			dimension_type_2d, bounds_dimension, description,
			"degree_north", NULL,
			read_geolocation_latitude_bounds);
    harp_variable_definition_set_valid_range_float(variable_definition, -90.0f, 90.0f);

    if (strcmp(product_type, "SN5_1B_NIR") == 0)
    {
        path = "/data/band3a/geolocation_data/latitude_bounds[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3a or band unset", NULL, path, NULL);
        path = "/data/band3b/geolocation_data/latitude_bounds[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3b", NULL, path, NULL);
        path = "/data/band3c/geolocation_data/latitude_bounds[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3c", NULL, path, NULL);
    }

    /* longitude_bounds */
    description = "The four longitude boundaries of each ground pixel on the WGS84 reference ellipsoid.";
    variable_definition =
	harp_ingestion_register_variable_full_read(product_definition,
			"longitude_bounds", harp_type_float, 2,
			dimension_type_2d, bounds_dimension, description,
			"degree_east", NULL,
			read_geolocation_longitude_bounds);
    harp_variable_definition_set_valid_range_float(variable_definition, -180.0f, 180.0f);

    if (strcmp(product_type, "SN5_1B_NIR") == 0)
    {
        path = "/data/band3a/geolocation_data/longitude[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3a or band unset", NULL, path, NULL);
        path = "/data/band3b/geolocation_data/longitude[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3b", NULL, path, NULL);
        path = "/data/band3c/geolocation_data/longitude[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3c", NULL, path, NULL);
    }

    /* satellite_altitude */
    description = "The altitude of the spacecraft relative to the WGS84 reference ellipsoid.";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "sensor_altitude", harp_type_int32, 1,
                                                   dimension_type_1d, NULL, description, 
        					   "m",
                                                   NULL, read_geolocation_satellite_altitude);
    //harp_variable_definition_set_valid_range_float(variable_definition, 700000.0f, 900000.0f);

    description = "the satellite altitude associated with a scanline is "
	    "repeated for each pixel in the scanline";

    if (strcmp(product_type, "SN5_1B_NIR") == 0)
    {
        path = "/data/band3a/geolocation_data/satellite_altitude[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3a or band unset", NULL, path, description);
        path = "/data/band3b/geolocation_data/satellite_altitude[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3b", NULL, path, description);
        path = "/data/band3c/geolocation_data/satellite_altitude[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3c", NULL, path, description);
    }

    /* satellite_latitude */
    description = "Latitude of the spacecraft sub-satellite point on the WGS84 reference ellipsoid.";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "sensor_latitude", harp_type_float, 1,
                                                   dimension_type_1d, NULL, description, "degree_north", NULL,
                                                   read_geolocation_satellite_latitude);
    harp_variable_definition_set_valid_range_float(variable_definition, -90.0f, 90.0f);
    description = "the satellite latitude associated with a scanline is repeated for each pixel in the scanline";
    if (strcmp(product_type, "SN5_1B_NIR") == 0)
    {
        path = "/data/band3a/geolocation_data/satellite_latitude[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3a or band unset", NULL, path, description);
        path = "/data/band3b/geolocation_data/satellite_latitude[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3b", NULL, path, description);
        path = "/data/band3c/geolocation_data/satellite_latitude[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3c", NULL, path, description);
    }

    /* satellite_longitude */
    description = "Longitude of the spacecraft sub-satellite point on the WGS84 reference ellipsoid.";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "sensor_longitude", harp_type_float, 1,
                                                   dimension_type_1d, NULL, description, "degree_east", NULL,
                                                   read_geolocation_satellite_longitude);
    harp_variable_definition_set_valid_range_float(variable_definition, -180.0f, 180.0f);
    description = "the satellite longitude associated with a scanline is repeated for each pixel in the scanline";

    if (strcmp(product_type, "SN5_1B_NIR") == 0)
    {
        path = "/data/band3a/geolocation_data/satellite_longitude[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3a or band unset", NULL, path, description);
        path = "/data/band3b/geolocation_data/satellite_longitude[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3b", NULL, path, description);
        path = "/data/band3c/geolocation_data/satellite_longitude[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3c", NULL, path, description);
    }

    /* satellite_orbit_phase */
    description = "Relative offset (0.0 ... 1.0) of the measurement in the orbit.";
    variable_definition =
	harp_ingestion_register_variable_full_read(product_definition,
			"sensor_orbit_phase", harp_type_float, 1,
			dimension_type_1d, NULL, description,
			HARP_UNIT_DIMENSIONLESS, NULL,
			read_geolocation_satellite_orbit_phase);
    description = "the satellite orbit phase associated with a scanline is repeated for each pixel in the scanline";
    if (strcmp(product_type, "SN5_1B_NIR") == 0)
    {
        path = "/data/band3a/geolocation_data/satellite_orbit_phase[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3a or band unset", NULL, path, description);
        path = "/data/band3b/geolocation_data/satellite_orbit_phase[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3b", NULL, path, description);
        path = "/data/band3c/geolocation_data/satellite_orbit_phase[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3c", NULL, path, description);
    }

    /* solar_zenith_angle */
    description = "Zenith angle of the sun at the ground pixel location on the WGS84 reference ellipsoid.";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "solar_zenith_angle", harp_type_float, 1,
                                                   dimension_type_1d, NULL, description, "degree", NULL,
                                                   read_geolocation_solar_zenith_angle);
    harp_variable_definition_set_valid_range_float(variable_definition, 0.0f, 180.0f);
    if (strcmp(product_type, "SN5_1B_NIR") == 0)
    {
        path = "/data/band3a/geolocation_data/solar_zenith_angle[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3a or band unset", NULL, path, NULL);
        path = "/data/band3b/geolocation_data/solar_zenith_angle[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3b", NULL, path, NULL);
        path = "/data/band3c/geolocation_data/solar_zenith_angle[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3c", NULL, path, NULL);
    }

    /* solar_azimuth_angle */
    description = "Azimuth angle of the sun at the ground pixel location on the WGS84 ellipsoid.";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "solar_azimuth_angle", harp_type_float, 1,
                                                   dimension_type_1d, NULL, description, "degree", NULL,
                                                   read_geolocation_solar_azimuth_angle);
    harp_variable_definition_set_valid_range_float(variable_definition, -180.0f, 180.0f);

    if (strcmp(product_type, "SN5_1B_NIR") == 0)
    {
        path = "/data/band3a/geolocation_data/solar_zenith_angle[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3a or band unset", NULL, path, NULL);
        path = "/data/band3b/geolocation_data/solar_zenith_angle[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3b", NULL, path, NULL);
        path = "/data/band3c/geolocation_data/solar_zenith_angle[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3c", NULL, path, NULL);
    }

    /* viewing_zenith_angle */
    description =
        "Zenith angle of the spacecraft at the ground pixel location on the WGS84 reference ellipsoid.";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "sensor_zenith_angle", harp_type_float, 1,
                                                   dimension_type_1d, NULL, description, "degree", NULL,
                                                   read_geolocation_viewing_zenith_angle);
    harp_variable_definition_set_valid_range_float(variable_definition, 0.0f, 180.0f);

    if (strcmp(product_type, "SN5_1B_NIR") == 0)
    {
        path = "/data/band3a/geolocation_data/viewing_zenith_angle[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3a or band unset", NULL, path, NULL);
        path = "/data/band3b/geolocation_data/viewing_zenith_angle[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3b", NULL, path, NULL);
        path = "/data/band3c/geolocation_data/viewing_zenith_angle[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3c", NULL, path, NULL);
    }

    /* viewing_azimuth_angle */
    description = "Azimuth angle of the spacecraft at the ground pixel location on the WGS84 reference ellipsoid."; 
    variable_definition =
	harp_ingestion_register_variable_full_read(product_definition,
			"sensor_azimuth_angle", harp_type_float, 1,
			dimension_type_1d, NULL, description, "degree", NULL,
			read_geolocation_viewing_azimuth_angle);
    harp_variable_definition_set_valid_range_float(variable_definition, -180.0f, 180.0f);

    if (strcmp(product_type, "SN5_1B_NIR") == 0)
    {
        path = "/data/band3a/geolocation_data/sensor_azimuth_angle[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3a or band unset", NULL, path, NULL);
        path = "/data/band3b/geolocation_data/sensor_azimuth_angle[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3b", NULL, path, NULL);
        path = "/data/band3c/geolocation_data/sensor_azimuth_angle[]";
        harp_variable_definition_add_mapping(variable_definition, "band=3c", NULL, path, NULL);
    }


}


static void register_nir_product(void)
{
    const char *path;
    const char *description;

    harp_ingestion_module *module;
    harp_product_definition *product_definition;
    harp_variable_definition *variable_definition;

    harp_dimension_type dimension_type_1d[1] = { harp_dimension_time };
    harp_dimension_type dimension_type_2d[2] = { harp_dimension_time, harp_dimension_independent };
    harp_dimension_type dimension_type_2d_spec[2] = { harp_dimension_time, harp_dimension_spectral };
    long bounds_dimension[2] = { -1, 4 };

    const char *band_option_values[3] = { "3a", "3b", "3c" };

    const char* bands_list[3] = {"band=3a or band unset", "band=3b", "band=3c"};
    int num_bands = sizeof(bands_list) / sizeof(bands_list[0]);


    /* Product Registration Phase */
    description = "Sentinel-5 L1b NIR radiance spectra";
    module = harp_ingestion_register_module("SN5_1B_NIR", "Sentinel-5", "EPS_SG", "SN5_1B_NIR",
                                            description, ingestion_init, ingestion_done);

    /* Option Registration Phase */ 
    description = "Choose which NIR band values to ingest: `band3a` (default), `band3b`, or `band3c`";
    harp_ingestion_register_option(module, "band",      /* option name */
                                   description, 3,      /* number of values */
                                   band_option_values); /* allowed values */

    /* harp_ingestion_register_product( module ptr, "ProductShortName", options table (NULL), dimension-callback ) */
    product_definition = harp_ingestion_register_product(module, "S5_1B_NIR", NULL, read_dimensions);

    /* Variables' Registration Phase */

    /* orbit_index */
    description = "absolute orbit number";
    variable_definition = harp_ingestion_register_variable_full_read(product_definition, "orbit_index", 
		    harp_type_int32, 0, NULL, NULL,
		    description, NULL, NULL, read_orbit_index);
    harp_variable_definition_add_mapping(variable_definition, NULL, NULL, "/@orbit_start", NULL);


    register_geolocation_variables(product_definition, "SN5_1B_NIR", bands_list, num_bands);








    /* Observation Variables */

    /* datetime_start */
    description = "Start time of the measurement.";
    variable_definition = 
	    harp_ingestion_register_variable_full_read(product_definition, "datetime_start", 
			                             harp_type_double, 1,
                                                     dimension_type_1d, NULL, description, 
						     "seconds since 2010-01-01", NULL,
                                                     read_datetime);

    description = "time converted from milliseconds since a reference time"
        "(given as seconds since 2010-01-01) to " 
	"seconds since" "2010-01-01 (using 86400 seconds per day)";

    path = "/data/band3a/observation_data/time, /data/band3a/observation_data/delta_time[]";
    harp_variable_definition_add_mapping(variable_definition, "band=3a or band unset", NULL, path, description);
    path = "/data/band3b/observation_data/time, /data/band3b/observation_data/delta_time[]";
    harp_variable_definition_add_mapping(variable_definition, "band=3b", NULL, path, description);
    path = "/data/band3c/observation_data/time, /data/band3c/observation_data/delta_time[]";
    harp_variable_definition_add_mapping(variable_definition, "band=3c", NULL, path, description);

    /* measurement_quality */
    description = "Overall quality information for a measurement.";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "measurement_quality", harp_type_int16, 1,
                                                   dimension_type_1d, NULL, description, 
        					   HARP_UNIT_DIMENSIONLESS,
                                                   NULL, read_observation_measurement_quality);

    description = "the measurement quality associated with a scanline is repeated for each pixel in the scanline";

    path = "/data/band3a/observation_data/measurement_quality[]";
    harp_variable_definition_add_mapping(variable_definition, "band=3a or band unset", NULL, path, description);
    path = "/data/band3b/observation_data/measurement_quality[]";
    harp_variable_definition_add_mapping(variable_definition, "band=3b", NULL, path, description);
    path = "/data/band3c/observation_data/measurement_quality[]";
    harp_variable_definition_add_mapping(variable_definition, "band=3c", NULL, path, description);

    /* radiance */ 
    description = "Measured spectral photon radiance for each spectral channel.";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, "photon_radiance", harp_type_float, 2,
                                                    dimension_type_2d_spec, NULL, description, 
        					    "mol/(s.m^2.nm.sr)", NULL, read_observation_radiance);
    path = "/data/band3a/observation_data/radiance[]";
    harp_variable_definition_add_mapping(variable_definition, "band=3a or band unset", NULL, path, NULL);
    path = "/data/band3b/observation_data/radiance[]";
    harp_variable_definition_add_mapping(variable_definition, "band=3b", NULL, path, NULL);
    path = "/data/band3c/observation_data/radiance[]";
    harp_variable_definition_add_mapping(variable_definition, "band=3c", NULL, path, NULL);

    //snprintf(path, MAX_PATH_LENGTH, "/%s/STANDARD_MODE/OBSERVATIONS/radiance[]", product_group_name);
    //harp_variable_definition_add_mapping(variable_definition, NULL, NULL, path, NULL);

    /* radiance_error */
    description = "Radiance error, encoded as 20 times the natural logarithmic "
	    "value of the absolute ratio between the radiance and the estimation "
	    "error.";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, 
			                           "photon_radiance_uncertainty_systematic",
                                                   harp_type_int8, 2, dimension_type_2d_spec, 
						   NULL, description,
                                                   "mol/(s.m^2.nm.sr)", 
						   NULL, read_observation_radiance_error);
    path = "/data/band3a/observation_data/radiance_error[]";
    harp_variable_definition_add_mapping(variable_definition, "band=3a or band unset", NULL, path, NULL);
    path = "/data/band3b/observation_data/radiance_error[]";
    harp_variable_definition_add_mapping(variable_definition, "band=3b", NULL, path, NULL);
    path = "/data/band3c/observation_data/radiance_error[]";
    harp_variable_definition_add_mapping(variable_definition, "band=3c", NULL, path, NULL);

    /* radiance_noise */
    description = "Random radiance error, encoded as 20 times the natural logarithmic "
	    "value of the absolute ratio between the radiance and the random error.";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, 
			                           "photon_radiance_uncertainty_random",
                                                   harp_type_int8, 2, dimension_type_2d_spec, 
						   NULL, description,
                                                   "mol/(s.m^2.nm.sr)", 
						   NULL, read_observation_radiance_noise);
    path = "/data/band3a/observation_data/radiance_noise[]";
    harp_variable_definition_add_mapping(variable_definition, "band=3a or band unset", NULL, path, NULL);
    path = "/data/band3b/observation_data/radiance_noise[]";
    harp_variable_definition_add_mapping(variable_definition, "band=3b", NULL, path, NULL);
    path = "/data/band3c/observation_data/radiance_noise[]";
    harp_variable_definition_add_mapping(variable_definition, "band=3c", NULL, path, NULL);

    /* spectral_channel_quality */
    description = "Quality assessment information for each (spectral) channel.";
    variable_definition =
        harp_ingestion_register_variable_full_read(product_definition, 
			                           "spectral_channel_quality",
                                                   harp_type_int8, 2, dimension_type_2d_spec, 
						   NULL, description,
                                                   HARP_UNIT_DIMENSIONLESS, 
						   NULL, read_observation_spectral_channel_quality);
    path = "/data/band3a/observation_data/spectral_channel_quality[]";
    harp_variable_definition_add_mapping(variable_definition, "band=3a or band unset", NULL, path, NULL);
    path = "/data/band3b/observation_data/spectral_channel_quality[]";
    harp_variable_definition_add_mapping(variable_definition, "band=3b", NULL, path, NULL);
    path = "/data/band3c/observation_data/spectral_channel_quality[]";
    harp_variable_definition_add_mapping(variable_definition, "band=3c", NULL, path, NULL);

    // TODO: Add Instrument Data

}


/* Entry point */
int harp_ingestion_module_s5_l1b_init(void)
{
    register_nir_product();

    return 0;
}

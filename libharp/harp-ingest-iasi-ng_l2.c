#include "coda.h"
#include "harp-ingestion.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Macro to determine the number of elements in a one dimensional C array. */
#define ARRAY_SIZE(X) (sizeof((X)) / sizeof((X)[0]))

/* Maximum length of a path string in generated mapping descriptions. */
#define MAX_PATH_LENGTH 256

typedef enum iasi_ng_product_type_enum {
  iasi_ng_type_co,
  iasi_ng_type_twv,
} iasi_ng_product_type;

#define IASI_NG_NUM_PRODUCT_TYPES (((int)iasi_ng_type_twv) + 1)

typedef enum iasi_ng_dimension_type_enum {
  iasi_ng_dim_lines,
  iasi_ng_dim_for,
  iasi_ng_dim_fov,
  iasi_ng_dim_level,
} iasi_ng_dimension_type;

/* handy constant: last enum value + 1 */
#define IASI_NG_NUM_DIM_TYPES ((int)iasi_ng_dim_level + 1)

static const char *
    iasi_ng_dimension_name[IASI_NG_NUM_PRODUCT_TYPES][IASI_NG_NUM_DIM_TYPES] = {
        {"n_lines", "n_for", "n_fov", NULL},       /* CO */
        {"n_lines", "n_for", "n_fov", "n_levels"}, /* TWV */
};

typedef struct ingest_info_struct {
  coda_product *product;

  iasi_ng_product_type product_type;

  /* dimensions */
  long num_lines;
  long num_for;
  long num_fov;
  long num_levels;

  /* cursors */
  coda_cursor product_cursor;
  coda_cursor geolocation_cursor;
  coda_cursor detailed_results_cursor;
  coda_cursor input_data_cursor;

} ingest_info;

/* The routines start here
 */

static const char *get_product_type_name(iasi_ng_product_type product_type) {
  switch (product_type) {
  case iasi_ng_type_co:
    return "IAS_02_CO";
  case iasi_ng_type_twv:
    return "IAS_02_TWV";
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
  for (i = 0; s[i] != '\0'; ++i) {
    if (s[i] == '-') {
      s[i] = '_';
    }
  }
}

static void broadcast_array_float(long num_scanlines, long num_pixels, float *data)
{
  long i;

  /* Repeat the value for each scanline for all pixels in that scanline. Iterate
   * in reverse to avoid overwriting scanline values.
   */
  for (i = num_scanlines - 1; i >= 0; i--) {
    long j;

    for (j = 0; j < num_pixels; j++) {
      data[i * num_pixels + j] = data[i];
    }
  }
}

static void broadcast_array_double(long num_scanlines, long num_pixels, double *data) 
{
  long i;

  /* Repeat the value for each scanline for all pixels in that scanline. Iterate
   * in reverse to avoid overwriting scanline values.
   */
  for (i = num_scanlines - 1; i >= 0; i--) {
    long j;

    for (j = 0; j < num_pixels; j++) {
      data[i * num_pixels + j] = data[i];
    }
  }
}

static int get_product_type(coda_product *product, iasi_ng_product_type *product_type)
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
    for (i = 0; i < IASI_NG_NUM_PRODUCT_TYPES; i++)
    {
        const char *code = get_product_type_name((iasi_ng_product_type)i);   

        if (strstr(buf, code) != NULL)
        {
            *product_type = (iasi_ng_product_type)i;
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


static void register_co_product(void)
{
    const char *path;
    const char *description;
}



int harp_ingestion_module_iasi_ng_l2_init(void)
{
    register_co_product();

    return 0;
}




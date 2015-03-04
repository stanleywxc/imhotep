#include <string.h>
#include <assert.h>
#include "table.h"

static int col_size_bytes(packed_table_t *table, int64_t max, int64_t min)
{
    uint64_t range = max - min;
    int bits = sizeof(range) * 8 - __builtin_clzl(range); /* !@# fix for zero case */
    if ((bits <= 1) && (table->n_boolean_cols == table->n_cols_aux_index)
            && (table->n_boolean_cols < MAX_BIT_FIELDS)) {
        table->n_boolean_cols++;
        return 0;
    }
    int size = ((bits - 1) / 8) + 1;
    return size;
}

/* Creates the starting and end indexes of the cols, where col/16 indicates
 * in which vector the col is.
 */
static void createColumnIndexes(
                                packed_table_t *table,
                                int n_cols,
                                int64_t * restrict col_mins,
                                int64_t * restrict col_maxes,
                                uint8_t first_free_byte)
{
    int col_offset = first_free_byte;    //Find the initial byte for the cols.
    int n_vectors = 1;
    int grp_stats_long_count = 0;
    int packed_stats_per_vec = 0;

    /* Pack the cols and create indexes to find where they start and end */
    for (int i = 0; i < n_cols; i++) {
        int col_size;

        col_size = col_size_bytes(table, col_maxes[i], col_mins[i]);
        packed_stats_per_vec ++;
        if (col_offset + col_size > n_vectors * 16) {
            col_offset = n_vectors * 16;
            n_vectors++;
            if ((packed_stats_per_vec & 0x1) == 1) {
                grp_stats_long_count ++;
            }
        }
        (table->index_cols)[2 * i] = col_offset;
        col_offset += col_size;
        (table->index_cols)[2 * i + 1] = col_offset;
        (table->col_2_vector)[i] = n_vectors - 1;
        table->n_cols_aux_index++;

        grp_stats_long_count ++;
    }
    table->unpadded_row_size = n_vectors;

    /* Calculate how many cols we have per vector */
    table->n_cols_per_vector = (uint8_t *) calloc(n_vectors, sizeof(uint8_t));

    /* Count how many non-bitfield cols in each packed vector */
    for (int i = table->n_boolean_cols; i < n_cols; i++) {
        (table->n_cols_per_vector)[(table->col_2_vector)[i]]++;
    }

    /*
     * Group cols row size must be 1 or a multiple of 2 vectors
     * to make preloading work properly
     */
    if (n_vectors == 1) {
        table->row_size = n_vectors;
    } else {
        /* round up to the next multiple of 2 */
        table->row_size = (n_vectors + 1) & (~0x1);
    }
}

//Create the array that after  can be used to get 2 cols at a time from the main
//vector array.**
//**except when there is an odd number of integer cols in the vector;
static void createShuffleVecFromIndexes(packed_table_t *table)
{
    uint8_t byte_vector[16];
    int k;
    uint8_t n_boolean_cols = table->n_boolean_cols;
    uint8_t n_cols = table->n_cols;
    uint16_t * index_cols = table->index_cols;
    uint8_t * col_n_vector = table->col_2_vector;

    table->shuffle_vecs_get2 = calloc(n_cols - n_boolean_cols, sizeof(__v16qi));
    table->shuffle_vecs_get1 = calloc(n_cols - n_boolean_cols, sizeof(__v16qi));
    int index2 = 0;
    int index1 = 0;

    for (int i = n_boolean_cols; i < n_cols; i++) {

        //clears the vector
        for (int j = 0; j < 16; j++) {
            byte_vector[j] = -1;
        }
        //creates the first part of the __m128i vector
        k = 0;
        for (int j = index_cols[2 * i]; j < index_cols[2 * i + 1]; j++) {
            byte_vector[k++] = j % 16;
        }
        table->shuffle_vecs_get1[index1++] = _mm_setr_epi8(byte_vector[0],
                                                byte_vector[1],
                                                byte_vector[2],
                                                byte_vector[3],
                                                byte_vector[4],
                                                byte_vector[5],
                                                byte_vector[6],
                                                byte_vector[7],
                                                -1,
                                                -1,
                                                -1,
                                                -1,
                                                -1,
                                                -1,
                                                -1,
                                                -1);

        //creates the second part of the vector, if the other col is in the same vector
        //as the first col. If not, just put -1s in this half of shuffle_vecs_get2
        k = 8;
        i++;
        if (i < n_cols && (col_n_vector[i] == col_n_vector[i - 1])) {
            for (int j = index_cols[2 * i]; j < index_cols[2 * i + 1]; j++) {
                byte_vector[k++] = j % 16;
            }
            table->shuffle_vecs_get1[index1++] = _mm_setr_epi8(byte_vector[8],
                                                    byte_vector[9],
                                                    byte_vector[10],
                                                    byte_vector[11],
                                                    byte_vector[12],
                                                    byte_vector[13],
                                                    byte_vector[14],
                                                    byte_vector[15],
                                                    -1,
                                                    -1,
                                                    -1,
                                                    -1,
                                                    -1,
                                                    -1,
                                                    -1,
                                                    -1);
        } else {
            i--;
        }
        table->shuffle_vecs_get2[index2++] = _mm_setr_epi8(byte_vector[0],
                                                byte_vector[1],
                                                byte_vector[2],
                                                byte_vector[3],
                                                byte_vector[4],
                                                byte_vector[5],
                                                byte_vector[6],
                                                byte_vector[7],
                                                byte_vector[8],
                                                byte_vector[9],
                                                byte_vector[10],
                                                byte_vector[11],
                                                byte_vector[12],
                                                byte_vector[13],
                                                byte_vector[14],
                                                byte_vector[15]);
    }
}

//Creates the shuffle and Blends vectors used to put cols inside the vector.
static void createShuffleBlendFromIndexes(packed_table_t *table)
{
    uint8_t byte_vector_shuffle[16];
    uint8_t byte_vector_blend[16];
    uint8_t n_boolean_cols = table->n_boolean_cols;
    uint8_t n_cols = table->n_cols;
    uint16_t * index_cols = table->index_cols;
    int k, i, j;
    table->shuffle_vecs_put = calloc((n_cols - n_boolean_cols), sizeof(__v16qi ));
    table->blend_vecs_put = calloc((n_cols - n_boolean_cols), sizeof(__v16qi ));

    //Creates the shuffle vectors to put each col in the right place for blending
    // And create the blend vectors. We will have a main vector that is gonna receive
    // all the cols one at a time by blending.
    // with the exception of the boolean cols.
    for (i = n_boolean_cols; i < n_cols; i++) {
        int index = i - n_boolean_cols;
        for (j = 0; j < 16; j++) {
            byte_vector_shuffle[j] = -1;
            byte_vector_blend[j] = 0;
        }
        k = 0;
        for (j = index_cols[2 * i]; j < index_cols[2 * i + 1]; j++) {
            byte_vector_shuffle[j % 16] = k++;
            byte_vector_blend[j % 16] = -1;
        }
        table->shuffle_vecs_put[index] = _mm_setr_epi8(  byte_vector_shuffle[0],
                                                byte_vector_shuffle[1],
                                                byte_vector_shuffle[2],
                                                byte_vector_shuffle[3],
                                                byte_vector_shuffle[4],
                                                byte_vector_shuffle[5],
                                                byte_vector_shuffle[6],
                                                byte_vector_shuffle[7],
                                                byte_vector_shuffle[8],
                                                byte_vector_shuffle[9],
                                                byte_vector_shuffle[10],
                                                byte_vector_shuffle[11],
                                                byte_vector_shuffle[12],
                                                byte_vector_shuffle[13],
                                                byte_vector_shuffle[14],
                                                byte_vector_shuffle[15]);

        table->blend_vecs_put[index] = _mm_setr_epi8(    byte_vector_blend[0],
                                            byte_vector_blend[1],
                                            byte_vector_blend[2],
                                            byte_vector_blend[3],
                                            byte_vector_blend[4],
                                            byte_vector_blend[5],
                                            byte_vector_blend[6],
                                            byte_vector_blend[7],
                                            byte_vector_blend[8],
                                            byte_vector_blend[9],
                                            byte_vector_blend[10],
                                            byte_vector_blend[11],
                                            byte_vector_blend[12],
                                            byte_vector_blend[13],
                                            byte_vector_blend[14],
                                            byte_vector_blend[15]);
    }
}

//This method assumes that the boolean cols will come first
packed_table_t *packed_table_create(int n_rows,
                                    int64_t *column_mins,
                                    int64_t *column_maxes,
                                    int n_cols)
{
    packed_table_t *table;

    table = (packed_table_t *)calloc(1, sizeof(packed_table_t));
    table->n_rows = n_rows;
    table->n_cols = n_cols;

    table->col_mins = (int64_t *) calloc(n_cols, sizeof(int64_t));
    /* copy in the mins */
    for (int i = 0; i < n_cols; i++) {
        table->col_mins[i] = column_mins[i];
    }
    table->index_cols = (uint16_t *) calloc(n_cols * 2, sizeof(uint16_t));
    table->col_2_vector = (uint8_t *) calloc(n_cols, sizeof(uint8_t));
    table->n_cols_aux_index = 0;
    table->n_boolean_cols = 0;
    createColumnIndexes(   table,
                        n_cols,
                        column_mins,
                        column_maxes,
                        (GROUP_SIZE + MAX_BIT_FIELDS + 7) / 8);
    createShuffleVecFromIndexes(table);
    createShuffleBlendFromIndexes(table);

    table->size = n_rows * table->row_size;
    table->data = (__v16qi *) aligned_alloc(64, sizeof(__v16qi) * table->size);
    memset(table->data, 0, sizeof(__v16qi) * table->size);

    return table;
}

void packed_table_destroy(packed_table_t *table)
{
    table->n_rows = -1;
    table->n_cols = -1;
    table->size = -1;

    free(table->index_cols);
    free(table->col_2_vector);
    free(table->col_mins);
    free(table->shuffle_vecs_put);
    free(table->blend_vecs_put);
    free(table->shuffle_vecs_get1);
    free(table->shuffle_vecs_get2);
    free(table->n_cols_per_vector);
    free(table->data);
    free(table);
}

/*
 * Attribute Getters
 */
int packed_table_get_size(packed_table_t *table)
{
    return table->n_rows * table->row_size;
}

int packed_table_get_row_size(packed_table_t *table)
{
    return table->row_size;
}

int packed_table_get_rows(packed_table_t *table)
{
    return table->n_rows;
}

int packed_table_get_cols(packed_table_t *table)
{
    return table->n_cols;
}


/*
 * Shared cell accessors and setters
 */
static inline void internal_set_cell(
                       packed_table_t* table,
                       int row,
                       int col,
                       long value,
                       uint8_t row_vector_index)
{
    size_t index = row * table->row_size;

    /* this makes the assumption that the first doc id is doc_id[0] */
    size_t vector_index = row_vector_index + index;

    /* Converts the data to a vector and shuffles the bytes into the correct spot */
    __m128i shuffled_col = _mm_shuffle_epi8(_mm_cvtsi64_si128(value),
                                            table->shuffle_vecs_put[col]);

    /* Inserts the new data into the packed data vector */
    __v16qi packed_data = table->data[vector_index];
    __v16qi updated_data = _mm_blendv_epi8(packed_data, shuffled_col, table->blend_vecs_put[col]);

    table->data[vector_index] = updated_data;
}

static inline void internal_set_boolean_cell(packed_table_t* table,
                               int row,
                               int col,
                               long value)
{
    __v16qi *packed_addr;
    uint32_t *store_address;
    int index = col * table->row_size + 0;

    packed_addr = &(table->data[index]);
    store_address = (uint32_t *)packed_addr;
    *store_address |= (value) << (GROUP_SIZE + col);
}

static inline int64_t internal_get_cell(
                          packed_table_t* table,
                          int row,
                          int column,
                          int row_size,
                          uint8_t col_vector)
{
    __v16qi packed;
    __v16qi shuffle_control_vec;
    __m128i unpacked;
    int64_t result;
    packed = (table->data)[row * row_size + col_vector];
    shuffle_control_vec = (table->shuffle_vecs_get1)[column];
    unpacked = _mm_shuffle_epi8(packed, shuffle_control_vec);
    result = _mm_extract_epi64(unpacked, 0);
    return result;
}

static inline int internal_get_boolean_cell(
                             packed_table_t* table,
                             int row,
                             int column,
                             int row_size)
{
    __v16qi* packed_addr;
    uint32_t* load_address;
    uint32_t bit;

    int index = row * row_size + 0;
    packed_addr = &table->data[index];
    load_address = (uint32_t*)packed_addr;
    bit = (*load_address) & (1 << (GROUP_SIZE + column));

    return (bit != 0);
}

static inline int internal_get_group(
                                     packed_table_t* table,
                                     int row,
                                     int row_size)
{
    __v16qi *packed_addr;
    const struct bit_fields_and_group *packed_bf_grp;
    const int index = row * row_size;

    packed_addr = &table->data[index];
    packed_bf_grp = (struct bit_fields_and_group *) packed_addr;
    return packed_bf_grp->grp;
}

static inline void internal_set_group(
                        packed_table_t* table,
                        int row,
                        int value)
{
    __v16qi *packed_addr;
    struct bit_fields_and_group *packed_bf_grp;
    const size_t index = row * table->row_size;

    packed_addr = &table->data[index];
    packed_bf_grp = (struct bit_fields_and_group *)packed_addr;
    packed_bf_grp->grp = value & GROUP_MASK;
}


/*
 * External cell accessors and getters
 */
long packed_table_get_cell(packed_table_t *table, int row, int column)
{
    uint8_t n_boolean_cols = table->n_boolean_cols;
    int row_size = table->row_size;
    int64_t min = (table->col_mins)[column];

    if (column >= n_boolean_cols) {
        uint8_t col_vector = (table->col_2_vector)[column];
        column -= n_boolean_cols;
        return internal_get_cell(table, row, column, row_size, col_vector) + min;
    }

    return internal_get_boolean_cell(table, row, column, row_size) + min;
}

void packed_table_set_cell(packed_table_t *table, int row, int col, long value)
{
    uint8_t packed_vector_index = (table->col_2_vector)[col];
    int64_t min = (table->col_mins)[col];

    if (col < table->n_boolean_cols) {
        internal_set_boolean_cell(table, row, col, value - min);
        return;
    }

    internal_set_cell(table, row, col, value, packed_vector_index);
}

long packed_table_get_group(packed_table_t *table, int row)
{
    int row_size = table->row_size;

    return internal_get_group(table, row, row_size);
}

void packed_table_set_group(packed_table_t *table, int row, long value)
{
    internal_set_group(table, row, value);
}


void packed_shard_batch_col_lookup( packed_table_t *table,
                                int * restrict row_ids,
                                int n_row_ids,
                                int64_t * restrict dest,
                                int column)
{
    uint8_t n_boolean_cols = table->n_boolean_cols;
    int row_size = table->row_size;
    int64_t min = (table->col_mins)[column];

    if (column >= n_boolean_cols) {
        uint8_t col_vector = (table->col_2_vector)[column];
        column -= n_boolean_cols;
        for (int i = 0; i < n_row_ids; i++) {
            int row = row_ids[i];
            dest[i] = internal_get_cell(table, row, column, row_size, col_vector) + min;
        }
        return;
    }

    for (int i = 0; i < n_row_ids; i++) {
        int row = row_ids[i];
        dest[i] = internal_get_boolean_cell(table, row, column, row_size) + min;
    }
}

void packed_shard_batch_set_col(   packed_table_t *table,
                            int * restrict row_ids,
                            int n_row_ids,
                            int64_t * restrict col_vals,
                            int col)
{
    int64_t min = (table->col_mins)[col];
    uint8_t packed_vector_index = (table->col_2_vector)[col];

    if (col < table->n_boolean_cols) {
        for (int i = 0; i < n_row_ids; i++) {
            internal_set_boolean_cell(table, row_ids[i], col, col_vals[i] - min);
        }
        return;
    }

    col -= table->n_boolean_cols;
    for (int i = 0; i < n_row_ids; i++) {
        internal_set_cell(table, row_ids[i], col, col_vals[i] - min, packed_vector_index);
    }
}

void packed_shard_batch_group_lookup( packed_table_t *table,
                                int * restrict row_ids,
                                int n_row_ids,
                                int64_t * restrict dest)
{
    int row_size = table->row_size;

    for (int i = 0; i < n_row_ids; i++) {
        int row = row_ids[i];
        dest[i] = internal_get_group(table, row, row_size);
    }
}

void packed_shard_batch_set_group(   packed_table_t *table,
                            int * restrict row_ids,
                            int n_row_ids,
                            int64_t * restrict group_vals)
{
    for (int i = 0; i < n_row_ids; i++) {
        int row = row_ids[i];
        internal_set_group(table, row, group_vals[i]);
    }
}

/*
 *
 *
 * Foo
 *
 *
 */

static inline void unpack_bit_fields(__v2di *dest_buffer,
                                    uint32_t bit_fields,
                                    uint8_t n_bit_fields)
{
    static __v2di lookup_table[4] = { { 0L, 0L }, { 0L, 1L }, { 1L, 0L }, { 1L, 1L } };

    for (int i = 0; i < n_bit_fields; i += 2) {
        dest_buffer[i / 2] = lookup_table[bit_fields & 3];
        bit_fields >>= 2;
    }
}

static inline __m128i unpack_2_metrics(__v16qi packed_data, __v16qi shuffle_vector)
{
    __m128i unpacked;

    unpacked = _mm_shuffle_epi8(packed_data, shuffle_vector);
    return unpacked;
}

static inline void unpack_vector(packed_table_t* src_table,
                                 __v16qi vector_data,
                                 int vector_num,
                                 __v2di *dest_buffer)
{
    int vector_index = 0;
    int n_cols = src_table->n_cols_per_vector[vector_num];
    __v16qi *shuffle_vecs = src_table->shuffle_vecs_get2;

    for (int k = 0; k < n_cols; k += 2) {
        __v2di data;

        data = unpack_2_metrics(vector_data, shuffle_vecs[vector_index]);
        /* save data into buffer */
        dest_buffer[vector_index] = data;
        vector_index++;
    }
}

static inline int core(packed_table_t* src_table,
                       unpacked_table_t* dest_table,
                       int col,
                       int vector_num,
                       __v16qi *src_row,
                       __v2di *dest_row)
{
    int offset_in_row = dest_table->col_offset[col];
    assert((offset_in_row % 2) == 0);  /* offset in row should be even */

    __v16qi vector = src_row[vector_num];
    unpack_vector(src_table, vector, vector_num, &(dest_row[offset_in_row / 2]));

    return src_table->n_cols_per_vector[vector_num];
}

inline void packed_table_unpack_row_to_table(
                                             packed_table_t* src_table,
                                             int src_row_id,
                                             unpacked_table_t* dest_table,
                                             int dest_row_id,
                                             int prefetch_row_id)
{
    __v16qi *src_row = &src_table->data[src_row_id * src_table->row_size];
    __v2di *dest_row = &dest_table->data[dest_row_id * dest_table->padded_row_len];

    /* unpack and save the bit field metrics */
    struct bit_fields_and_group packed_bf_g = *((struct bit_fields_and_group *)src_row);
    unpack_bit_fields(dest_row, packed_bf_g.cols, src_table->n_boolean_cols);

    /* flag row as modified */
    bit_tree_set(&dest_table->non_zero_rows, packed_bf_g.grp);

    /* loop through row elements */
    int vector_num;
    int column_idx = src_table->n_boolean_cols;
    int n_packed_vecs = src_table->unpadded_row_size;
    for (vector_num = 0; vector_num < n_packed_vecs - 4; vector_num += 4)
    {
        column_idx += core(src_table, dest_table, column_idx, vector_num = 0, src_row, dest_row);
        column_idx += core(src_table, dest_table, column_idx, vector_num + 1, src_row, dest_row);
        column_idx += core(src_table, dest_table, column_idx, vector_num + 2, src_row, dest_row);
        column_idx += core(src_table, dest_table, column_idx, vector_num + 3, src_row, dest_row);

        /* prefetch once per cache line */
        {
            __v16qi *prefetch_addr = &src_table->data[prefetch_row_id * src_table->row_size
                                                     + vector_num];
            PREFETCH(prefetch_addr);
        }
    }

    /* prefetch the final cache line */
    if (vector_num < n_packed_vecs) {
        __v16qi *prefetch_addr = &src_table->data[prefetch_row_id * src_table->row_size
                                                 + vector_num];
        PREFETCH(prefetch_addr);
    }

    /* loop through the remaining row elements */
    for (; vector_num < n_packed_vecs; vector_num ++)
    {
        column_idx += core(src_table, dest_table, column_idx, vector_num, src_row, dest_row);
    }
}
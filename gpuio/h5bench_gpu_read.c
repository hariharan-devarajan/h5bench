/****** Copyright Notice ***
 *
 * PIOK - Parallel I/O Kernels - VPIC-IO, VORPAL-IO, and GCRM-IO, Copyright
 * (c) 2015, The Regents of the University of California, through Lawrence
 * Berkeley National Laboratory (subject to receipt of any required
 * approvals from the U.S. Dept. of Energy).  All rights reserved.
 *
 * If you have questions about your rights to use or distribute this
 * software, please contact Berkeley Lab's Innovation & Partnerships Office
 * at  IPO@lbl.gov.
 *
 * NOTICE.  This Software was developed under funding from the U.S.
 * Department of Energy and the U.S. Government consequently retains
 * certain rights. As such, the U.S. Government has been granted for itself
 * and others acting on its behalf a paid-up, nonexclusive, irrevocable,
 * worldwide license in the Software to reproduce, distribute copies to the
 * public, prepare derivative works, and perform publicly and display
 * publicly, and to permit other to do so.
 *
 ****************************/

/**
 *
 * Email questions to SByna@lbl.gov
 * Scientific Data Management Research Group
 * Lawrence Berkeley National Laboratory
 *
 */

// Description: This is a simple benchmark based on VPIC's I/O interface
//      Each process reads a specified number of particles into
//      a hdf5 output file using only HDF5 calls
// Author:  Suren Byna <SByna@lbl.gov>
//      Lawrence Berkeley National Laboratory, Berkeley, CA
// Created: in 2011
// Modified:    01/06/2014 --> Removed all H5Part calls and using HDF5 calls
//              02/19/2019 --> Add option to read multiple timesteps of data - Tang

#include <math.h>
#include <hdf5.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include "../commons/h5bench_util.h"
#include "../commons/async_adaptor.h"

#include "mem/include/metamem_api.h"
#include "mem/include/metamem_pch.h"

// Global Variables and dimensions
long long  NUM_PARTICLES = 0, FILE_OFFSET = 0;
long long  TOTAL_PARTICLES = 0;
async_mode ASYNC_MODE;
int        NUM_RANKS, MY_RANK, NUM_TIMESTEPS;
hid_t      PARTICLE_COMPOUND_TYPE;
hid_t      PARTICLE_COMPOUND_TYPE_SEPARATES[8];

herr_t          ierr;
data_contig_md *BUF_STRUCT;
mem_monitor *   MEM_MONITOR;

metamem **mm;

void
print_data(int n)
{
    int i;
    for (i = 0; i < n; i++)
        printf("sample data: %f %f %f %d %f %f %f %f\n", BUF_STRUCT->x[i], BUF_STRUCT->y[i], BUF_STRUCT->z[i],
               BUF_STRUCT->id_1[i], BUF_STRUCT->id_2[i], BUF_STRUCT->px[i], BUF_STRUCT->py[i],
               BUF_STRUCT->pz[i]);
}

// Create HDF5 file and read data
void
read_h5_data(time_step *ts, hid_t loc, hid_t *dset_ids, hid_t filespace, hid_t memspace,
             unsigned long *read_time, unsigned long *metadata_time)
{
    hid_t         dapl;
    unsigned long t1, t2, t3;
    dapl = H5Pcreate(H5P_DATASET_ACCESS);
#if H5_VERSION_GE(1, 10, 0)
    H5Pset_all_coll_metadata_ops(dapl, true);
#endif

    t1 = get_time_usec();

    dset_ids[0] = H5Dopen_async(loc, "x", dapl, ts->es_meta_create);
    dset_ids[1] = H5Dopen_async(loc, "y", dapl, ts->es_meta_create);
    dset_ids[2] = H5Dopen_async(loc, "z", dapl, ts->es_meta_create);
    dset_ids[3] = H5Dopen_async(loc, "id_1", dapl, ts->es_meta_create);
    dset_ids[4] = H5Dopen_async(loc, "id_2", dapl, ts->es_meta_create);
    dset_ids[5] = H5Dopen_async(loc, "px", dapl, ts->es_meta_create);
    dset_ids[6] = H5Dopen_async(loc, "py", dapl, ts->es_meta_create);
    dset_ids[7] = H5Dopen_async(loc, "pz", dapl, ts->es_meta_create);

    t2 = get_time_usec();

    ierr = H5Dread_async(dset_ids[0], H5T_NATIVE_FLOAT, memspace, filespace, H5P_DEFAULT, BUF_STRUCT->x,
                         ts->es_data);
    ierr = H5Dread_async(dset_ids[1], H5T_NATIVE_FLOAT, memspace, filespace, H5P_DEFAULT, BUF_STRUCT->y,
                         ts->es_data);
    ierr = H5Dread_async(dset_ids[2], H5T_NATIVE_FLOAT, memspace, filespace, H5P_DEFAULT, BUF_STRUCT->z,
                         ts->es_data);
    ierr = H5Dread_async(dset_ids[3], H5T_NATIVE_INT, memspace, filespace, H5P_DEFAULT, BUF_STRUCT->id_1,
                         ts->es_data);
    ierr = H5Dread_async(dset_ids[4], H5T_NATIVE_INT, memspace, filespace, H5P_DEFAULT, BUF_STRUCT->id_2,
                         ts->es_data);
    ierr = H5Dread_async(dset_ids[5], H5T_NATIVE_FLOAT, memspace, filespace, H5P_DEFAULT, BUF_STRUCT->px,
                         ts->es_data);
    ierr = H5Dread_async(dset_ids[6], H5T_NATIVE_FLOAT, memspace, filespace, H5P_DEFAULT, BUF_STRUCT->py,
                         ts->es_data);
    ierr = H5Dread_async(dset_ids[7], H5T_NATIVE_FLOAT, memspace, filespace, H5P_DEFAULT, BUF_STRUCT->pz,
                         ts->es_data);

    t3 = get_time_usec();

    *read_time     = t3 - t2;
    *metadata_time = t2 - t1;

    if (MY_RANK == 0)
        printf("  Read 8 variable completed\n");
    H5Pclose(dapl);
}

int
_set_dataspace_seq_read(unsigned long read_elem_cnt, hid_t *filespace_in, hid_t *memspace_out)
{
    *memspace_out = H5Screate_simple(1, (hsize_t *)&read_elem_cnt, NULL);
    H5Sselect_hyperslab(*filespace_in, H5S_SELECT_SET, (hsize_t *)&FILE_OFFSET, NULL,
                        (hsize_t *)&read_elem_cnt, NULL);
    return read_elem_cnt;
}

// returns actual rounded read element count.
unsigned long
_set_dataspace_strided_read(unsigned long read_elem_cnt, bench_params params, hid_t *filespace_in,
                            hid_t *memspace_out)
{
    if (MY_RANK == 0) {
        printf("Stride parameters: STRIDE_SIZE = %lu, BLOCK_SIZE = %lu, BLOCK_CNT = %lu\n", params.stride,
               params.block_size, params.block_cnt);
    }

    if ((params.stride + params.block_size) * params.block_cnt > params.dim_1) {
        printf("\n\nInvalid hyperslab setting: (STRIDE_SIZE + BLOCK_SIZE) * BLOCK_CNT"
               "must be no greater than the number of available particles per rank(%lu).\n\n",
               params.chunk_dim_1);
        return 0;
    }

    unsigned long actual_elem_cnt = params.block_size * params.block_cnt;
    *memspace_out                 = H5Screate_simple(1, (hsize_t *)&actual_elem_cnt, NULL);

    H5Sselect_hyperslab(*filespace_in, H5S_SELECT_SET,
                        (hsize_t *)&FILE_OFFSET,        // start-offset
                        (hsize_t *)&params.stride,      // stride
                        (hsize_t *)&params.block_cnt,   // block cnt
                        (hsize_t *)&params.block_size); // block size

    return actual_elem_cnt;
}

// filespace should be read from the file first, then select the hyperslab.
unsigned long
_set_dataspace_seq_2D(hid_t *filespace_in_out, hid_t *memspace_out, unsigned long long dim_1,
                      unsigned long long dim_2)
{
    hsize_t mem_dims[2], file_dims[2];
    mem_dims[0] = (hsize_t)dim_1;
    mem_dims[1] = (hsize_t)dim_2;

    file_dims[0] = (hsize_t)dim_1 * NUM_RANKS; // total x length: dim_1 * world_size.
    file_dims[1] = (hsize_t)dim_2;             // always the same dim_2

    hsize_t file_starts[2], count[2];   // select start point and range in each dimension.
    file_starts[0] = dim_1 * (MY_RANK); // file offset for each rank
    file_starts[1] = 0;

    count[0]      = dim_1;
    count[1]      = dim_2;
    *memspace_out = H5Screate_simple(2, mem_dims, NULL);
    H5Sselect_hyperslab(*filespace_in_out, H5S_SELECT_SET, file_starts, NULL, count, NULL);
    return dim_1 * dim_2;
}

unsigned long
_set_dataspace_seq_3D(hid_t *filespace_in_out, hid_t *memspace_out, unsigned long long dim_1,
                      unsigned long long dim_2, unsigned long long dim_3)
{
    hsize_t mem_dims[3];
    hsize_t file_dims[3];
    mem_dims[0]  = (hsize_t)dim_1;
    mem_dims[1]  = (hsize_t)dim_2;
    mem_dims[2]  = (hsize_t)dim_3;
    file_dims[0] = (hsize_t)dim_1 * NUM_RANKS;
    file_dims[1] = (hsize_t)dim_2;
    file_dims[2] = (hsize_t)dim_3;

    hsize_t file_starts[3], file_range[3]; // select start point and range in each dimension.
    file_starts[0] = dim_1 * (MY_RANK);
    file_starts[1] = 0;
    file_starts[2] = 0;
    file_range[0]  = dim_1;
    file_range[1]  = dim_2;
    file_range[2]  = dim_3;

    *memspace_out = H5Screate_simple(3, mem_dims, NULL);

    H5Sselect_hyperslab(*filespace_in_out, H5S_SELECT_SET, file_starts, NULL, file_range, NULL);
    return dim_1 * dim_2 * dim_3;
}

hid_t
get_filespace(hid_t file_id)
{
    char *grp_name  = "/Timestep_0";
    char *ds_name   = "px";
    hid_t gid       = H5Gopen2(file_id, grp_name, H5P_DEFAULT);
    hid_t dsid      = H5Dopen2(gid, ds_name, H5P_DEFAULT);
    hid_t filespace = H5Dget_space(dsid);
    H5Dclose(dsid);
    H5Gclose(gid);
    return filespace;
}

unsigned long
set_dataspace(bench_params params, unsigned long long try_read_elem_cnt, hid_t *filespace_in_out,
              hid_t *memspace_out)
{
    unsigned long actual_read_cnt = 0;
    switch (params.access_pattern.pattern_read) {
        case CONTIG_1D:

            _set_dataspace_seq_read(try_read_elem_cnt, filespace_in_out, memspace_out);
            actual_read_cnt = try_read_elem_cnt;
            break;

        case STRIDED_1D:
            actual_read_cnt =
                _set_dataspace_strided_read(try_read_elem_cnt, params, filespace_in_out, memspace_out);
            break;

        case CONTIG_2D:
            actual_read_cnt =
                _set_dataspace_seq_2D(filespace_in_out, memspace_out, params.dim_1, params.dim_2);
            break;

        case CONTIG_3D:
            actual_read_cnt = _set_dataspace_seq_3D(filespace_in_out, memspace_out, params.dim_1,
                                                    params.dim_2, params.dim_3);
            break;

        default:
            printf("Unknown read pattern\n");
            break;
    }
    return actual_read_cnt;
}

int
_run_benchmark_read(hid_t file_id, hid_t fapl, hid_t gapl, hid_t filespace, bench_params params,
                    unsigned long *total_data_size_out, unsigned long *raw_read_time_out,
                    unsigned long *inner_metadata_time, unsigned long *h2d_time_total,
                    unsigned long *d2h_time_total)
{
    *raw_read_time_out               = 0;
    *inner_metadata_time             = 0;
    *h2d_time_total                  = 0;
    *d2h_time_total                  = 0;
    int                nts           = params.cnt_time_step;
    unsigned long long read_elem_cnt = params.try_num_particles;
    hid_t              grp;
    char               grp_name[128];
    unsigned long      rt1 = 0, rt2 = 0;
    unsigned long      actual_read_cnt = 0;
    hid_t              memspace;
    actual_read_cnt = set_dataspace(params, read_elem_cnt, &filespace, &memspace);

    if (actual_read_cnt < 1)
        return -1;

#if H5_VERSION_GE(1, 13, 0)
    if (H5VLis_connector_registered_by_name("async")) {
        if (MY_RANK == 0) {
            printf("Using 'async' VOL connector\n");
        }
    }
#endif

    if (MY_RANK == 0) {
        print_params(&params);
    }

    MEM_MONITOR      = mem_monitor_new(nts, ASYNC_MODE, actual_read_cnt, params.io_mem_limit);
    unsigned long t1 = 0, t2 = 0, t3 = 0, t4 = 0;
    unsigned long t5 = 0, t6 = 0, t7 = 0, t8 = 0;

    unsigned long meta_time1 = 0, meta_time2 = 0, meta_time3 = 0, meta_time4 = 0, meta_time5;
    unsigned long read_time_exp = 0, metadata_time_exp = 0;
    unsigned long read_time_imp = 0, metadata_time_imp = 0;
    int           dset_cnt = 8;
    for (int ts_index = 0; ts_index < nts; ts_index++) {
        meta_time1 = 0, meta_time2 = 0, meta_time3 = 0, meta_time4 = 0, meta_time5 = 0;
        sprintf(grp_name, "Timestep_%d", ts_index);
        time_step *ts = &(MEM_MONITOR->time_steps[ts_index]);
        MEM_MONITOR->mem_used += ts->mem_size;
        assert(ts);

        if (params.cnt_time_step_delay > 0) {
            if (ts_index > params.cnt_time_step_delay - 1) // delayed close on all ids of the previous ts
                ts_delayed_close(MEM_MONITOR, &meta_time1, dset_cnt);
        }
        mem_monitor_check_run(MEM_MONITOR, &meta_time2, &read_time_imp);

        t1 = get_time_usec();

        ts->grp_id = H5Gopen_async(file_id, grp_name, gapl, ts->es_meta_create);

        t2         = get_time_usec();
        meta_time3 = (t2 - t1);

        if (MY_RANK == 0)
            printf("Reading %s ... \n", grp_name);

        read_h5_data(ts, ts->grp_id, ts->dset_ids, filespace, memspace, &read_time_exp, &meta_time4);

        if (MY_RANK == 0)
            printf("H2D %s ... \n", grp_name);

        // Metamemory
        t5 = get_time_usec();
        mm[0]->fn->copy(mm[0], H2D);
        mm[1]->fn->copy(mm[1], H2D);
        mm[2]->fn->copy(mm[2], H2D);
        mm[3]->fn->copy(mm[3], H2D);
        mm[4]->fn->copy(mm[4], H2D);
        mm[5]->fn->copy(mm[5], H2D);
        mm[6]->fn->copy(mm[6], H2D);
        mm[7]->fn->copy(mm[7], H2D);
        t6 = get_time_usec();

        // TODO: compute here

        ts->status = TS_DELAY;

        if (params.cnt_time_step_delay == 0) {
            t3 = get_time_usec();
            for (int j = 0; j < dset_cnt; j++)
                H5Dclose_async(ts->dset_ids[j], ts->es_meta_close);
            H5Gclose_async(ts->grp_id, ts->es_meta_close);
            ts->status = TS_READY;
            t4         = get_time_usec();
            meta_time5 = (t4 - t3);
        }

        if (ts_index != nts - 1) { // no sleep after the last ts
            if (params.compute_time.time_num >= 0) {
                if (MY_RANK == 0)
                    printf("Computing... \n");
                async_sleep(file_id, fapl, params.compute_time);
            }
        }

        if (MY_RANK == 0)
            printf("D2H %s ... \n", grp_name);

        // Metamemory
        t7 = get_time_usec();
        mm[0]->fn->copy(mm[0], D2H);
        mm[1]->fn->copy(mm[1], D2H);
        mm[2]->fn->copy(mm[2], D2H);
        mm[3]->fn->copy(mm[3], D2H);
        mm[4]->fn->copy(mm[4], D2H);
        mm[5]->fn->copy(mm[5], D2H);
        mm[6]->fn->copy(mm[6], D2H);
        mm[7]->fn->copy(mm[7], D2H);
        t8 = get_time_usec();

        *h2d_time_total += (t6 - t5);
        *d2h_time_total += (t8 - t7);
        *raw_read_time_out += (read_time_exp + read_time_imp);
        *inner_metadata_time += (meta_time1 + meta_time2 + meta_time3 + meta_time4 + meta_time5);
    }

    mem_monitor_final_run(MEM_MONITOR, &metadata_time_imp, &read_time_imp);
    *raw_read_time_out += read_time_imp;
    *inner_metadata_time += metadata_time_imp;
    *total_data_size_out = nts * actual_read_cnt * (6 * sizeof(float) + 2 * sizeof(int));
    H5Sclose(memspace);
    H5Sclose(filespace);
    return 0;
}

void
set_pl(hid_t *fapl, hid_t *gapl)
{
    *fapl = H5Pcreate(H5P_FILE_ACCESS);
    H5Pset_fapl_mpio(*fapl, MPI_COMM_WORLD, MPI_INFO_NULL);
#if H5_VERSION_GE(1, 10, 0)
    H5Pset_all_coll_metadata_ops(*fapl, true);
    H5Pset_coll_metadata_write(*fapl, true);
#endif
    *gapl = H5Pcreate(H5P_GROUP_ACCESS);
#if H5_VERSION_GE(1, 10, 0)
    H5Pset_all_coll_metadata_ops(*gapl, true);
#endif
}

void
print_usage(char *name)
{
    printf("Usage: %s /path/to/file #timestep sleep_sec [# mega particles]\n", name);
}

data_contig_md *
prepare_data_multi_dim(unsigned long long dim_1, unsigned long long dim_2, unsigned long long dim_3)
{

    // MetaMemory
    mm = (metamem **)malloc(8 * sizeof(metamem *));
    for (int i = 0; i < 8; i++) {
#if defined(METAMEM_USE_CUDA)
        mm[i] = metamem_init(METAMEM_CUDA);
#elif defined(METAMEM_USE_HIP)
        mm[i] = metamem_init(METAMEM_HIP);
#else
        mm[i] = metamem_init(METAMEM_POSIX);
#endif
    }

    data_contig_md *buf_struct       = (data_contig_md *)malloc(sizeof(data_contig_md));
    buf_struct->dim_1                = dim_1;
    buf_struct->dim_2                = dim_2;
    buf_struct->dim_3                = dim_3;
    unsigned long long num_particles = dim_1 * dim_2 * dim_3;

    buf_struct->particle_cnt = num_particles;

    // MetaMemory
    mm[0]->fn->alloc(mm[0], num_particles, sizeof(float), MEM_CPU_PAGEABLE, MEM_GPU);
    mm[1]->fn->alloc(mm[1], num_particles, sizeof(float), MEM_CPU_PAGEABLE, MEM_GPU);
    mm[2]->fn->alloc(mm[2], num_particles, sizeof(float), MEM_CPU_PAGEABLE, MEM_GPU);
    mm[3]->fn->alloc(mm[3], num_particles, sizeof(float), MEM_CPU_PAGEABLE, MEM_GPU);
    mm[4]->fn->alloc(mm[4], num_particles, sizeof(float), MEM_CPU_PAGEABLE, MEM_GPU);
    mm[5]->fn->alloc(mm[5], num_particles, sizeof(float), MEM_CPU_PAGEABLE, MEM_GPU);
    mm[6]->fn->alloc(mm[6], num_particles, sizeof(int), MEM_CPU_PAGEABLE, MEM_GPU);
    mm[7]->fn->alloc(mm[7], num_particles, sizeof(float), MEM_CPU_PAGEABLE, MEM_GPU);

    buf_struct->x    = (float *)mm[0]->host_ptr->ptr;
    buf_struct->y    = (float *)mm[1]->host_ptr->ptr;
    buf_struct->z    = (float *)mm[2]->host_ptr->ptr;
    buf_struct->px   = (float *)mm[3]->host_ptr->ptr;
    buf_struct->py   = (float *)mm[4]->host_ptr->ptr;
    buf_struct->pz   = (float *)mm[5]->host_ptr->ptr;
    buf_struct->id_1 = (int *)mm[6]->host_ptr->ptr;
    buf_struct->id_2 = (float *)mm[7]->host_ptr->ptr;

    return buf_struct;
}

int
main(int argc, char *argv[])
{
    int mpi_thread_lvl_provided = -1;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &mpi_thread_lvl_provided);
    assert(MPI_THREAD_MULTIPLE == mpi_thread_lvl_provided);
    MPI_Comm_rank(MPI_COMM_WORLD, &MY_RANK);
    MPI_Comm_size(MPI_COMM_WORLD, &NUM_RANKS);

    int sleep_time = 0;

    bench_params params;

    char *cfg_file_path = argv[1];
    char *file_name     = argv[2]; // data file to read

    if (MY_RANK == 0)
        printf("config file: %s, read data file: %s\n", argv[1], argv[2]);
    int do_write = 0;
    if (read_config(cfg_file_path, &params, do_write) < 0) {
        if (MY_RANK == 0)
            printf("Config file read failed. check path: %s\n", cfg_file_path);
        return 0;
    }
    ASYNC_MODE    = params.asyncMode;
    NUM_TIMESTEPS = params.cnt_time_step;

    if (NUM_TIMESTEPS <= 0) {
        if (MY_RANK == 0)
            printf("Usage: ./%s /path/to/file #timestep [# mega particles]\n", argv[0]);
        return 0;
    }

    if (params.io_op != IO_READ) {
        if (MY_RANK == 0)
            printf("Make sure the configuration file has IO_OPERATION=READ defined\n");
        return 0;
    }

    hid_t fapl, gapl;
    set_pl(&fapl, &gapl);

    hsize_t dims[64] = {0};

    hid_t         file_id;
    if (params.file_per_proc) {
      char mpi_rank_output_file_path[4096];
      sprintf(mpi_rank_output_file_path, "%s/rank_%d_%s", get_dir_from_path(file_name), MY_RANK,
        get_file_name_from_path(file_name));

      file_id = H5Fopen(mpi_rank_output_file_path, H5F_ACC_RDONLY, fapl);
    }
    else {
      file_id = H5Fopen(file_name, H5F_ACC_RDONLY, fapl);
    }
    hid_t         filespace       = get_filespace(file_id);
    int           dims_cnt        = H5Sget_simple_extent_dims(filespace, dims, NULL);
    unsigned long total_particles = 1;
    if (dims_cnt > 0) {
        for (int i = 0; i < dims_cnt; i++) {
            if (MY_RANK == 0)
                printf("dims[%d] = %lu (total number for the file)\n", i, dims[i]);
            total_particles *= dims[i];
        }
    }
    else {
        if (MY_RANK == 0)
            printf("Failed to read dimensions. \n");
        return 0;
    }

    if (dims_cnt > 0) { // 1D
        if (params.dim_1 > dims[0] / NUM_RANKS) {
            if (MY_RANK == 0)
                printf("Failed: Required dimension(%lu) is greater than the allowed dimension per rank "
                       "(%llu).\n",
                       params.dim_1, dims[0] / NUM_RANKS);
            goto error;
        }
    }
    if (dims_cnt > 1) { // 2D
        if (params.dim_2 > dims[1]) {
            if (MY_RANK == 0)
                printf("Failed: Required dimension_2(%lu) is greater than file dimension(%lu).\n",
                       params.dim_2, dims[1]);
            goto error;
        }
    }
    if (dims_cnt > 2) { // 3D
        if (params.dim_2 > dims[1]) {
            if (MY_RANK == 0)
                printf("Failed: Required dimension_3(%lu) is greater than file dimension(%lu).\n",
                       params.dim_3, dims[2]);
            goto error;
        }
    }

    NUM_PARTICLES = total_particles / NUM_RANKS;

    unsigned long long read_elem_cnt = params.try_num_particles;

    if (read_elem_cnt > NUM_PARTICLES) {
        if (MY_RANK == 0)
            printf("read_elem_cnt_m <= num_particles must hold.\n");
        return 0;
    }

    MPI_Info info = MPI_INFO_NULL;
    if (MY_RANK == 0) {
        printf("Total particles in the file: %lu\n", total_particles);
        printf("Number of particles available per rank: %llu \n", NUM_PARTICLES);
    }

    MPI_Barrier(MPI_COMM_WORLD);

    MPI_Allreduce(&NUM_PARTICLES, &TOTAL_PARTICLES, 1, MPI_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);
    MPI_Scan(&NUM_PARTICLES, &FILE_OFFSET, 1, MPI_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);
    FILE_OFFSET -= NUM_PARTICLES;
    BUF_STRUCT = prepare_data_multi_dim(params.dim_1, params.dim_2, params.dim_3);

    unsigned long t1 = get_time_usec();

    if (file_id < 0) {
        if (MY_RANK == 0)
            printf("Error with opening file [%s]!\n", file_name);
        goto done;
    }

    if (MY_RANK == 0)
        printf("Opened HDF5 file ... [%s]\n", file_name);

    unsigned long raw_read_time, metadata_time, local_data_size;
    unsigned long raw_h2d_time, raw_d2h_time;

    int ret = _run_benchmark_read(file_id, fapl, gapl, filespace, params, &local_data_size, &raw_read_time,
                                  &metadata_time, &raw_h2d_time, &raw_d2h_time);

    if (ret < 0) {
        if (MY_RANK == 0)
            printf("_run_benchmark_read() failed.\n");

        goto error;
    }
    MPI_Barrier(MPI_COMM_WORLD);

    H5Pclose(fapl);
    H5Pclose(gapl);
    H5Fclose_async(file_id, 0);

    MPI_Barrier(MPI_COMM_WORLD);
    unsigned long t4 = get_time_usec();

    // MetaMemory
    mm[0]->fn->free(mm[0]);
    mm[1]->fn->free(mm[1]);
    mm[2]->fn->free(mm[2]);
    mm[3]->fn->free(mm[3]);
    mm[4]->fn->free(mm[4]);
    mm[5]->fn->free(mm[5]);
    mm[6]->fn->free(mm[6]);
    mm[7]->fn->free(mm[7]);

    metamem_shutdown(mm[0]);
    metamem_shutdown(mm[1]);
    metamem_shutdown(mm[2]);
    metamem_shutdown(mm[3]);
    metamem_shutdown(mm[4]);
    metamem_shutdown(mm[5]);
    metamem_shutdown(mm[6]);
    metamem_shutdown(mm[7]);

    if (MY_RANK == 0) {
        char *mode_str = NULL;

        if (has_vol_async) {
            mode_str = "ASYNC";
        }
        else {
            mode_str = "SYNC";
        }

        printf("\n =================  Performance results  =================\n");
        unsigned long long total_sleep_time_us =
            read_time_val(params.compute_time, TIME_US) * (params.cnt_time_step - 1);
        unsigned long total_size_gb = NUM_RANKS * local_data_size / (1024 * 1024 * 1024);
        printf("Total emulated compute time = %.3lf sec\n"
               "Total read size = %lu GB\n",
               total_sleep_time_us / (1000.0 * 1000.0), total_size_gb);

        float h2d_s            = (float)raw_h2d_time / (1000 * 1000);
        float raw_h2d_rate_gbs = (float)total_size_gb / h2d_s;
        printf("Raw h2d time = %.3f sec\n", h2d_s);

        float d2h_s            = (float)raw_d2h_time / (1000 * 1000);
        float raw_d2h_rate_gbs = (float)total_size_gb / d2h_s;
        printf("Raw d2h time = %.3f sec\n", d2h_s);

        float rrt_s = (float)raw_read_time / (1000 * 1000);

        float raw_rate_gbs = total_size_gb / rrt_s;
        printf("Raw read time = %.3f sec \n", rrt_s);

        float meta_time_s = (float)metadata_time / (1000 * 1000);
        printf("Metadata time = %.3f sec\n", meta_time_s);

        float oct_s = (float)(t4 - t1) / (1000 * 1000);
        printf("Observed read completion time = %.3f sec\n", oct_s);

        printf("%s Raw h2d rate = %.3f GB/sec \n", mode_str, raw_h2d_rate_gbs);
        printf("%s Raw d2h rate = %.3f GB/sec \n", mode_str, raw_d2h_rate_gbs);
        printf("%s Raw read rate = %.3f GB/sec \n", mode_str, raw_rate_gbs);
        double or_gbs = (float)total_size_gb / ((float)(t4 - t1 - total_sleep_time_us) / (1000 * 1000));
        printf("%s Observed read rate = %.6f GB/sec\n", mode_str, or_gbs);

        if (params.useCSV) {
            fprintf(params.csv_fs, "NUM_RANKS, %d\n", NUM_RANKS);
            fprintf(params.csv_fs, "Total emulated compute time, %llu, sec\n",
                    total_sleep_time_us / (1000 * 1000));
            fprintf(params.csv_fs, "Total read size, %lu, GB\n", total_size_gb);
            fprintf(params.csv_fs, "Raw_h2d_time, %.3f, sec\n", h2d_s);
            fprintf(params.csv_fs, "Raw_h2d_rate, %.3f, GB/sec\n", raw_h2d_rate_gbs);
            fprintf(params.csv_fs, "Raw_d2h_time, %.3f, sec\n", d2h_s);
            fprintf(params.csv_fs, "Raw_d2h_rate, %.3f, GB/sec\n", raw_d2h_rate_gbs);
            fprintf(params.csv_fs, "Metadata_time, %.3f, sec\n", meta_time_s);
            fprintf(params.csv_fs, "Raw read time, %.3f, sec\n", rrt_s);
            fprintf(params.csv_fs, "Observed completion time, %.3f, sec\n", oct_s);

            fprintf(params.csv_fs, "Raw read rate, %.3f, GB/sec\n", raw_rate_gbs);
            fprintf(params.csv_fs, "Observed read rate, %.3f, GB/sec\n", or_gbs);
            fclose(params.csv_fs);
        }
    }

error:
    H5E_BEGIN_TRY
    {
        H5Fclose(file_id);
        H5Pclose(fapl);
    }
    H5E_END_TRY;

done:
    H5close();
    MPI_Finalize();
    return 0;
}
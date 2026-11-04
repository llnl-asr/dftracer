======================
MPI support
======================

DFTracer traces MPI and MPI-IO calls through `brahma <https://github.com/llnl-asr/brahma>`_,
which splits them into two layers:

* The **common** functions have the same signature in every MPI brahma was generated from.
  They are compiled with no implementation or version guards, so they are traced for every
  MPI implementation and version, including ones not listed on this page.
* The **compat** functions are the ones an MPI family has beyond the common set, for example
  the MPI-4 persistent collectives and the large-count ``_c`` calls. Each family
  (implementation plus major.minor version) has its own compat interface, compiled only when
  the build's MPI belongs to that family.

Every function on this page is recorded as an event of type ``10`` (``MPI``). The ``cat``
column holds the family the function belongs to, shown in the tables below, or ``MPIIO`` for
MPI-IO. Two functions are intercepted but never recorded: ``MPI_Initialized`` and
``MPI_Finalized``. DFTracer's logger calls them itself, and recording them would recurse.

The lists on this page come from the generated tracers under ``src/dftracer/core/brahma``
(``mpi.cpp``, ``mpiio.cpp`` and the ``*_compat.cpp`` files). Those files are produced by
``tools/generate_interfaces_from_brahma.py`` from brahma's interface headers, and record in
their header comment which MPI releases the interfaces were generated from. Regenerating them
can change these lists.

Which interfaces a build uses
*******************************

At configure time DFTracer probes the MPI compiler and prints the implementation and version
it found, for example::

    -- [dftracer] dependency: MPI C probe: impl=OPENMPI brahma_version=500003

The common interface is always compiled in. The compat interface is chosen by implementation
and major.minor: OpenMPI 5.0.3 uses ``openmpi_5_0``, MPICH 4.3.0 has none and gets the common
functions only. Set ``DFTRACER_MPI_IMPL`` to override the detected implementation.

To see what an installed DFTracer selected, run ``dftracer_install_info``. Its MPI section
lists ``common_calls_traced``, the ``compat_interface`` and ``mpiio_compat_interface`` of this
build (empty if there is none), every compat interface available, and the MPI releases they
were generated from. A family is available only when brahma ships its interface and DFTracer has
a tracer for it; ``compat_brahma_only`` lists families this brahma has that DFTracer has not been
regenerated for, and ``compat_tracer_only`` the reverse. All of it is read at configure time from
brahma's installed headers and ``src/dftracer/core/brahma/``, so adding a family needs no edit to
the tool.

Supported MPI versions
*******************************

.. list-table::
   :header-rows: 1
   :widths: 14 22 20 18 26

   * - Implementation
     - Compat interface (MPI)
     - Compat interface (MPI-IO)
     - Generated from
     - Tested
   * - OpenMPI
     - 4.1.x (1 function), 5.0.x (35)
     - none
     - 4.1.6, 5.0.3, 5.0.6
     - 4.1.6, 4.1.8, 5.0.3, 5.0.6
   * - MPICH
     - 3.4.x (2), 4.2.x (186)
     - 4.2.x (29)
     - 3.4.3, 4.2.3
     - 3.4.3, 4.2.3, 4.3.0 (common only)
   * - MVAPICH2
     - 2.3.x (2)
     - none
     - 2.3.7
     - not in CI: the spack build of 2.3.7-1 fails
   * - Cray MPICH
     - 8.1.x (2), 9.0.x (163), 9.1.x (185)
     - 9.0.x (29), 9.1.x (29)
     - 8.1.8, 8.1.33, 9.0.1, 9.1.0
     - not in CI: proprietary
   * - Any other MPI
     - none: common functions only
     - none: common functions only
     - n/a
     - n/a

"Tested" lists the releases the ``mpi`` job of ``.github/workflows/hdf5-mpi-trace-ci.yml``
builds and runs: ``test_c_mpi`` under the preload, checking that the MPI and MPI-IO calls it
makes are in the trace. They were also run with IOR's MPI-IO backend in the same container,
checking ``MPI_File_open``, ``MPI_File_write_at_all``, ``MPI_File_read_at_all`` and
``MPI_File_close`` in the trace.

A compat interface covers every patch release of its family, but its functions are the ones
present in every release it was generated from. A patch release that was not read (OpenMPI
4.1.8, for example) gets the compat functions it shares with those releases. A function it adds
on top is not traced unless it is also common. To add an MPI release or implementation,
regenerate brahma as its README describes ("Adding an MPI release or implementation"), then
regenerate DFTracer's tracers with ``tools/generate_interfaces_from_brahma.py``.

Rank and run metadata
*******************************

Each rank's trace records its rank in ``MPI_COMM_WORLD`` once, right after ``MPI_Init`` or
``MPI_Init_thread`` returns, as a ``PR`` record named ``rank`` and a ``process_name`` record
``Rank <n>``. A trace file belongs to one process, so the rank applies to the whole file. This
and the other metadata DFTracer writes (host, file and string hashes, the ``start`` and ``end``
records, application metadata) are described in :ref:`Metadata Events`.

Files in MPI-IO events
*******************************

An ``MPI_File`` handle is never written to the trace: it is an opaque pointer in OpenMPI and
ROMIO, so its value means nothing outside the process. Instead MPI-IO events carry ``fhash``,
the same file key POSIX events carry:

* ``MPI_File_open`` hashes the path into ``fhash`` (writing its ``FH`` record the first time the
  path is seen), records it, and on success remembers that the returned handle refers to it.
* Every call that takes an open handle (``MPI_File_write_at_all``, ``MPI_File_read``,
  ``MPI_File_set_view``, the large-count ``_c`` calls, ...) records that ``fhash``.
* ``MPI_File_close`` records the ``fhash`` and forgets the handle, which MPI may reuse.
* ``MPI_File_delete`` records the ``fhash`` of the path it deletes.

So the MPI-IO and POSIX events for one file share an ``fhash``, and an ``FH`` record resolves it
to the path. A handle that was not opened through a traced ``MPI_File_open`` (one opened before
tracing started) has no ``fhash``, and its events carry none. The Fortran handle conversions
(``MPI_File_c2f``, ``MPI_File_f2c``) are not intercepted.

Common functions (every MPI)
*******************************

301 MPI functions and 59 MPI-IO functions, traced for every MPI implementation and version.

.. list-table::
   :header-rows: 1
   :widths: 50 20 10

   * - Family
     - ``cat`` in the trace
     - Functions
   * - Environment and initialization
     - ``env``
     - 22
   * - Point-to-point and requests
     - ``p2p``
     - 45
   * - Collectives and reduction operations
     - ``collective``
     - 48
   * - One-sided (RMA)
     - ``rma``
     - 47
   * - Datatypes and packing
     - ``datatype``
     - 47
   * - Communicators, groups and error handlers
     - ``comm``
     - 50
   * - Topologies
     - ``topology``
     - 20
   * - Dynamic processes
     - ``spawn``
     - 12
   * - Info objects
     - ``metadata``
     - 10
   * - MPI-IO
     - ``MPIIO``
     - 59

**Environment and initialization** (22): ``MPI_Abort``, ``MPI_Add_error_class``, ``MPI_Add_error_code``, ``MPI_Add_error_string``, ``MPI_Alloc_mem``, ``MPI_Error_class``, ``MPI_Error_string``, ``MPI_Finalize``, ``MPI_Finalized``, ``MPI_Free_mem``, ``MPI_Get_count``, ``MPI_Get_elements``, ``MPI_Get_elements_x``, ``MPI_Get_library_version``, ``MPI_Get_processor_name``, ``MPI_Get_version``, ``MPI_Init``, ``MPI_Init_thread``, ``MPI_Initialized``, ``MPI_Query_thread``, ``MPI_Wtick``, ``MPI_Wtime``

**Point-to-point and requests** (45): ``MPI_Bsend``, ``MPI_Bsend_init``, ``MPI_Buffer_attach``, ``MPI_Buffer_detach``, ``MPI_Cancel``, ``MPI_Grequest_complete``, ``MPI_Grequest_start``, ``MPI_Ibsend``, ``MPI_Improbe``, ``MPI_Imrecv``, ``MPI_Iprobe``, ``MPI_Irecv``, ``MPI_Irsend``, ``MPI_Is_thread_main``, ``MPI_Isend``, ``MPI_Issend``, ``MPI_Mprobe``, ``MPI_Mrecv``, ``MPI_Probe``, ``MPI_Recv``, ``MPI_Recv_init``, ``MPI_Request_free``, ``MPI_Request_get_status``, ``MPI_Rsend``, ``MPI_Rsend_init``, ``MPI_Send``, ``MPI_Send_init``, ``MPI_Sendrecv``, ``MPI_Sendrecv_replace``, ``MPI_Ssend``, ``MPI_Ssend_init``, ``MPI_Start``, ``MPI_Startall``, ``MPI_Status_set_cancelled``, ``MPI_Status_set_elements``, ``MPI_Status_set_elements_x``, ``MPI_Test``, ``MPI_Test_cancelled``, ``MPI_Testall``, ``MPI_Testany``, ``MPI_Testsome``, ``MPI_Wait``, ``MPI_Waitall``, ``MPI_Waitany``, ``MPI_Waitsome``

**Collectives and reduction operations** (48): ``MPI_Allgather``, ``MPI_Allgatherv``, ``MPI_Allreduce``, ``MPI_Alltoall``, ``MPI_Alltoallv``, ``MPI_Alltoallw``, ``MPI_Barrier``, ``MPI_Bcast``, ``MPI_Exscan``, ``MPI_Gather``, ``MPI_Gatherv``, ``MPI_Iallgather``, ``MPI_Iallgatherv``, ``MPI_Iallreduce``, ``MPI_Ialltoall``, ``MPI_Ialltoallv``, ``MPI_Ialltoallw``, ``MPI_Ibarrier``, ``MPI_Ibcast``, ``MPI_Iexscan``, ``MPI_Igather``, ``MPI_Igatherv``, ``MPI_Ineighbor_allgather``, ``MPI_Ineighbor_allgatherv``, ``MPI_Ineighbor_alltoall``, ``MPI_Ineighbor_alltoallv``, ``MPI_Ineighbor_alltoallw``, ``MPI_Ireduce``, ``MPI_Ireduce_scatter``, ``MPI_Ireduce_scatter_block``, ``MPI_Iscan``, ``MPI_Iscatter``, ``MPI_Iscatterv``, ``MPI_Neighbor_allgather``, ``MPI_Neighbor_allgatherv``, ``MPI_Neighbor_alltoall``, ``MPI_Neighbor_alltoallv``, ``MPI_Neighbor_alltoallw``, ``MPI_Op_commutative``, ``MPI_Op_create``, ``MPI_Op_free``, ``MPI_Reduce``, ``MPI_Reduce_local``, ``MPI_Reduce_scatter``, ``MPI_Reduce_scatter_block``, ``MPI_Scan``, ``MPI_Scatter``, ``MPI_Scatterv``

**One-sided (RMA)** (47): ``MPI_Accumulate``, ``MPI_Compare_and_swap``, ``MPI_Fetch_and_op``, ``MPI_Get``, ``MPI_Get_accumulate``, ``MPI_Put``, ``MPI_Raccumulate``, ``MPI_Rget``, ``MPI_Rget_accumulate``, ``MPI_Rput``, ``MPI_Win_allocate``, ``MPI_Win_allocate_shared``, ``MPI_Win_attach``, ``MPI_Win_call_errhandler``, ``MPI_Win_complete``, ``MPI_Win_create``, ``MPI_Win_create_dynamic``, ``MPI_Win_create_errhandler``, ``MPI_Win_create_keyval``, ``MPI_Win_delete_attr``, ``MPI_Win_detach``, ``MPI_Win_fence``, ``MPI_Win_flush``, ``MPI_Win_flush_all``, ``MPI_Win_flush_local``, ``MPI_Win_flush_local_all``, ``MPI_Win_free``, ``MPI_Win_free_keyval``, ``MPI_Win_get_attr``, ``MPI_Win_get_errhandler``, ``MPI_Win_get_group``, ``MPI_Win_get_info``, ``MPI_Win_get_name``, ``MPI_Win_lock``, ``MPI_Win_lock_all``, ``MPI_Win_post``, ``MPI_Win_set_attr``, ``MPI_Win_set_errhandler``, ``MPI_Win_set_info``, ``MPI_Win_set_name``, ``MPI_Win_shared_query``, ``MPI_Win_start``, ``MPI_Win_sync``, ``MPI_Win_test``, ``MPI_Win_unlock``, ``MPI_Win_unlock_all``, ``MPI_Win_wait``

**Datatypes and packing** (47): ``MPI_Address``, ``MPI_Get_address``, ``MPI_Pack``, ``MPI_Pack_external``, ``MPI_Pack_external_size``, ``MPI_Pack_size``, ``MPI_Type_commit``, ``MPI_Type_contiguous``, ``MPI_Type_create_darray``, ``MPI_Type_create_f90_complex``, ``MPI_Type_create_f90_integer``, ``MPI_Type_create_f90_real``, ``MPI_Type_create_hindexed``, ``MPI_Type_create_hindexed_block``, ``MPI_Type_create_hvector``, ``MPI_Type_create_indexed_block``, ``MPI_Type_create_keyval``, ``MPI_Type_create_resized``, ``MPI_Type_create_struct``, ``MPI_Type_create_subarray``, ``MPI_Type_delete_attr``, ``MPI_Type_dup``, ``MPI_Type_extent``, ``MPI_Type_free``, ``MPI_Type_free_keyval``, ``MPI_Type_get_attr``, ``MPI_Type_get_contents``, ``MPI_Type_get_envelope``, ``MPI_Type_get_extent``, ``MPI_Type_get_extent_x``, ``MPI_Type_get_name``, ``MPI_Type_get_true_extent``, ``MPI_Type_get_true_extent_x``, ``MPI_Type_hindexed``, ``MPI_Type_hvector``, ``MPI_Type_indexed``, ``MPI_Type_lb``, ``MPI_Type_match_size``, ``MPI_Type_set_attr``, ``MPI_Type_set_name``, ``MPI_Type_size``, ``MPI_Type_size_x``, ``MPI_Type_struct``, ``MPI_Type_ub``, ``MPI_Type_vector``, ``MPI_Unpack``, ``MPI_Unpack_external``

**Communicators, groups and error handlers** (50): ``MPI_Attr_delete``, ``MPI_Attr_get``, ``MPI_Attr_put``, ``MPI_Comm_call_errhandler``, ``MPI_Comm_compare``, ``MPI_Comm_create``, ``MPI_Comm_create_errhandler``, ``MPI_Comm_create_group``, ``MPI_Comm_create_keyval``, ``MPI_Comm_delete_attr``, ``MPI_Comm_dup``, ``MPI_Comm_dup_with_info``, ``MPI_Comm_free``, ``MPI_Comm_free_keyval``, ``MPI_Comm_get_attr``, ``MPI_Comm_get_errhandler``, ``MPI_Comm_get_info``, ``MPI_Comm_get_name``, ``MPI_Comm_group``, ``MPI_Comm_idup``, ``MPI_Comm_rank``, ``MPI_Comm_remote_group``, ``MPI_Comm_remote_size``, ``MPI_Comm_set_attr``, ``MPI_Comm_set_errhandler``, ``MPI_Comm_set_info``, ``MPI_Comm_set_name``, ``MPI_Comm_size``, ``MPI_Comm_split``, ``MPI_Comm_split_type``, ``MPI_Comm_test_inter``, ``MPI_Errhandler_free``, ``MPI_Errhandler_get``, ``MPI_Errhandler_set``, ``MPI_Group_compare``, ``MPI_Group_difference``, ``MPI_Group_excl``, ``MPI_Group_free``, ``MPI_Group_incl``, ``MPI_Group_intersection``, ``MPI_Group_range_excl``, ``MPI_Group_range_incl``, ``MPI_Group_rank``, ``MPI_Group_size``, ``MPI_Group_translate_ranks``, ``MPI_Group_union``, ``MPI_Intercomm_create``, ``MPI_Intercomm_merge``, ``MPI_Keyval_create``, ``MPI_Keyval_free``

**Topologies** (20): ``MPI_Cart_coords``, ``MPI_Cart_create``, ``MPI_Cart_get``, ``MPI_Cart_map``, ``MPI_Cart_rank``, ``MPI_Cart_shift``, ``MPI_Cart_sub``, ``MPI_Cartdim_get``, ``MPI_Dims_create``, ``MPI_Dist_graph_create``, ``MPI_Dist_graph_create_adjacent``, ``MPI_Dist_graph_neighbors``, ``MPI_Dist_graph_neighbors_count``, ``MPI_Graph_create``, ``MPI_Graph_get``, ``MPI_Graph_map``, ``MPI_Graph_neighbors``, ``MPI_Graph_neighbors_count``, ``MPI_Graphdims_get``, ``MPI_Topo_test``

**Dynamic processes** (12): ``MPI_Close_port``, ``MPI_Comm_accept``, ``MPI_Comm_connect``, ``MPI_Comm_disconnect``, ``MPI_Comm_get_parent``, ``MPI_Comm_join``, ``MPI_Comm_spawn``, ``MPI_Comm_spawn_multiple``, ``MPI_Lookup_name``, ``MPI_Open_port``, ``MPI_Publish_name``, ``MPI_Unpublish_name``

**Info objects** (10): ``MPI_Info_create``, ``MPI_Info_delete``, ``MPI_Info_dup``, ``MPI_Info_free``, ``MPI_Info_get``, ``MPI_Info_get_nkeys``, ``MPI_Info_get_nthkey``, ``MPI_Info_get_valuelen``, ``MPI_Info_set``, ``MPI_Register_datarep``

**MPI-IO** (59): ``MPI_File_call_errhandler``, ``MPI_File_close``, ``MPI_File_create_errhandler``, ``MPI_File_delete``, ``MPI_File_get_amode``, ``MPI_File_get_atomicity``, ``MPI_File_get_byte_offset``, ``MPI_File_get_errhandler``, ``MPI_File_get_group``, ``MPI_File_get_info``, ``MPI_File_get_position``, ``MPI_File_get_position_shared``, ``MPI_File_get_size``, ``MPI_File_get_type_extent``, ``MPI_File_get_view``, ``MPI_File_iread``, ``MPI_File_iread_all``, ``MPI_File_iread_at``, ``MPI_File_iread_at_all``, ``MPI_File_iread_shared``, ``MPI_File_iwrite``, ``MPI_File_iwrite_all``, ``MPI_File_iwrite_at``, ``MPI_File_iwrite_at_all``, ``MPI_File_iwrite_shared``, ``MPI_File_open``, ``MPI_File_preallocate``, ``MPI_File_read``, ``MPI_File_read_all``, ``MPI_File_read_all_begin``, ``MPI_File_read_all_end``, ``MPI_File_read_at``, ``MPI_File_read_at_all``, ``MPI_File_read_at_all_begin``, ``MPI_File_read_at_all_end``, ``MPI_File_read_ordered``, ``MPI_File_read_ordered_begin``, ``MPI_File_read_ordered_end``, ``MPI_File_read_shared``, ``MPI_File_seek``, ``MPI_File_seek_shared``, ``MPI_File_set_atomicity``, ``MPI_File_set_errhandler``, ``MPI_File_set_info``, ``MPI_File_set_size``, ``MPI_File_set_view``, ``MPI_File_sync``, ``MPI_File_write``, ``MPI_File_write_all``, ``MPI_File_write_all_begin``, ``MPI_File_write_all_end``, ``MPI_File_write_at``, ``MPI_File_write_at_all``, ``MPI_File_write_at_all_begin``, ``MPI_File_write_at_all_end``, ``MPI_File_write_ordered``, ``MPI_File_write_ordered_begin``, ``MPI_File_write_ordered_end``, ``MPI_File_write_shared``

Compat functions (per MPI family)
**********************************

These are traced in addition to the common functions, only when the build's MPI is in the
family.

OpenMPI 4.1.x
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Communicators, groups and error handlers** (1): ``MPI_Errhandler_create``

OpenMPI 5.0.x
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

35 MPI-4 functions: persistent collectives, partitioned point-to-point, and the newer
communicator and info calls. OpenMPI 5.0.x has no MPI-IO compat interface and no large-count
``_c`` functions in its compat set.

**Point-to-point and requests** (8): ``MPI_Isendrecv``, ``MPI_Isendrecv_replace``, ``MPI_Parrived``, ``MPI_Pready``, ``MPI_Pready_list``, ``MPI_Pready_range``, ``MPI_Precv_init``, ``MPI_Psend_init``

**Collectives and reduction operations** (22): ``MPI_Allgather_init``, ``MPI_Allgatherv_init``, ``MPI_Allreduce_init``, ``MPI_Alltoall_init``, ``MPI_Alltoallv_init``, ``MPI_Alltoallw_init``, ``MPI_Barrier_init``, ``MPI_Bcast_init``, ``MPI_Exscan_init``, ``MPI_Gather_init``, ``MPI_Gatherv_init``, ``MPI_Neighbor_allgather_init``, ``MPI_Neighbor_allgatherv_init``, ``MPI_Neighbor_alltoall_init``, ``MPI_Neighbor_alltoallv_init``, ``MPI_Neighbor_alltoallw_init``, ``MPI_Reduce_init``, ``MPI_Reduce_scatter_block_init``, ``MPI_Reduce_scatter_init``, ``MPI_Scan_init``, ``MPI_Scatter_init``, ``MPI_Scatterv_init``

**Communicators, groups and error handlers** (3): ``MPI_Comm_create_from_group``, ``MPI_Comm_idup_with_info``, ``MPI_Intercomm_create_from_groups``

**Info objects** (2): ``MPI_Info_create_env``, ``MPI_Info_get_string``

MPICH 3.4.x, MVAPICH2 2.3.x and Cray MPICH 8.1.x
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Datatypes and packing** (2): ``MPI_Aint_add``, ``MPI_Aint_diff``

MPICH 4.2.x
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

186 MPI functions and 29 MPI-IO functions:

* the 35 functions of the OpenMPI 5.0.x compat interface above;
* 23 more functions, listed below;
* 128 large-count ``_c`` functions, listed below.

**Environment and initialization** (4): ``MPI_Get_hw_resource_info``, ``MPI_Remove_error_class``, ``MPI_Remove_error_code``, ``MPI_Remove_error_string``

**Point-to-point and requests** (11): ``MPI_Buffer_flush``, ``MPI_Buffer_iflush``, ``MPI_Request_get_status_all``, ``MPI_Request_get_status_any``, ``MPI_Request_get_status_some``, ``MPI_Status_get_error``, ``MPI_Status_get_source``, ``MPI_Status_get_tag``, ``MPI_Status_set_error``, ``MPI_Status_set_source``, ``MPI_Status_set_tag``

**Datatypes and packing** (3): ``MPI_Aint_add``, ``MPI_Aint_diff``, ``MPI_Type_get_value_index``

**Communicators, groups and error handlers** (5): ``MPI_Comm_attach_buffer``, ``MPI_Comm_detach_buffer``, ``MPI_Comm_flush_buffer``, ``MPI_Comm_iflush_buffer``, ``MPI_Errhandler_create``

**Large-count** (128): ``MPI_Accumulate_c``, ``MPI_Allgather_c``, ``MPI_Allgather_init_c``, ``MPI_Allgatherv_c``, ``MPI_Allgatherv_init_c``, ``MPI_Allreduce_c``, ``MPI_Allreduce_init_c``, ``MPI_Alltoall_c``, ``MPI_Alltoall_init_c``, ``MPI_Alltoallv_c``, ``MPI_Alltoallv_init_c``, ``MPI_Alltoallw_c``, ``MPI_Alltoallw_init_c``, ``MPI_Bcast_c``, ``MPI_Bcast_init_c``, ``MPI_Bsend_c``, ``MPI_Bsend_init_c``, ``MPI_Buffer_attach_c``, ``MPI_Buffer_detach_c``, ``MPI_Comm_attach_buffer_c``, ``MPI_Comm_detach_buffer_c``, ``MPI_Exscan_c``, ``MPI_Exscan_init_c``, ``MPI_Gather_c``, ``MPI_Gather_init_c``, ``MPI_Gatherv_c``, ``MPI_Gatherv_init_c``, ``MPI_Get_accumulate_c``, ``MPI_Get_c``, ``MPI_Get_count_c``, ``MPI_Get_elements_c``, ``MPI_Iallgather_c``, ``MPI_Iallgatherv_c``, ``MPI_Iallreduce_c``, ``MPI_Ialltoall_c``, ``MPI_Ialltoallv_c``, ``MPI_Ialltoallw_c``, ``MPI_Ibcast_c``, ``MPI_Ibsend_c``, ``MPI_Iexscan_c``, ``MPI_Igather_c``, ``MPI_Igatherv_c``, ``MPI_Imrecv_c``, ``MPI_Ineighbor_allgather_c``, ``MPI_Ineighbor_allgatherv_c``, ``MPI_Ineighbor_alltoall_c``, ``MPI_Ineighbor_alltoallv_c``, ``MPI_Ineighbor_alltoallw_c``, ``MPI_Irecv_c``, ``MPI_Ireduce_c``, ``MPI_Ireduce_scatter_block_c``, ``MPI_Ireduce_scatter_c``, ``MPI_Irsend_c``, ``MPI_Iscan_c``, ``MPI_Iscatter_c``, ``MPI_Iscatterv_c``, ``MPI_Isend_c``, ``MPI_Isendrecv_c``, ``MPI_Isendrecv_replace_c``, ``MPI_Issend_c``, ``MPI_Mrecv_c``, ``MPI_Neighbor_allgather_c``, ``MPI_Neighbor_allgather_init_c``, ``MPI_Neighbor_allgatherv_c``, ``MPI_Neighbor_allgatherv_init_c``, ``MPI_Neighbor_alltoall_c``, ``MPI_Neighbor_alltoall_init_c``, ``MPI_Neighbor_alltoallv_c``, ``MPI_Neighbor_alltoallv_init_c``, ``MPI_Neighbor_alltoallw_c``, ``MPI_Neighbor_alltoallw_init_c``, ``MPI_Op_create_c``, ``MPI_Pack_c``, ``MPI_Pack_external_c``, ``MPI_Pack_external_size_c``, ``MPI_Pack_size_c``, ``MPI_Put_c``, ``MPI_Raccumulate_c``, ``MPI_Recv_c``, ``MPI_Recv_init_c``, ``MPI_Reduce_c``, ``MPI_Reduce_init_c``, ``MPI_Reduce_local_c``, ``MPI_Reduce_scatter_block_c``, ``MPI_Reduce_scatter_block_init_c``, ``MPI_Reduce_scatter_c``, ``MPI_Reduce_scatter_init_c``, ``MPI_Register_datarep_c``, ``MPI_Rget_accumulate_c``, ``MPI_Rget_c``, ``MPI_Rput_c``, ``MPI_Rsend_c``, ``MPI_Rsend_init_c``, ``MPI_Scan_c``, ``MPI_Scan_init_c``, ``MPI_Scatter_c``, ``MPI_Scatter_init_c``, ``MPI_Scatterv_c``, ``MPI_Scatterv_init_c``, ``MPI_Send_c``, ``MPI_Send_init_c``, ``MPI_Sendrecv_c``, ``MPI_Sendrecv_replace_c``, ``MPI_Ssend_c``, ``MPI_Ssend_init_c``, ``MPI_Status_set_elements_c``, ``MPI_Type_contiguous_c``, ``MPI_Type_create_darray_c``, ``MPI_Type_create_hindexed_block_c``, ``MPI_Type_create_hindexed_c``, ``MPI_Type_create_hvector_c``, ``MPI_Type_create_indexed_block_c``, ``MPI_Type_create_resized_c``, ``MPI_Type_create_struct_c``, ``MPI_Type_create_subarray_c``, ``MPI_Type_get_contents_c``, ``MPI_Type_get_envelope_c``, ``MPI_Type_get_extent_c``, ``MPI_Type_get_true_extent_c``, ``MPI_Type_indexed_c``, ``MPI_Type_size_c``, ``MPI_Type_vector_c``, ``MPI_Unpack_c``, ``MPI_Unpack_external_c``, ``MPI_Win_allocate_c``, ``MPI_Win_allocate_shared_c``, ``MPI_Win_create_c``, ``MPI_Win_shared_query_c``

**MPI-IO, large-count** (29): ``MPI_File_get_type_extent_c``, ``MPI_File_iread_all_c``, ``MPI_File_iread_at_all_c``, ``MPI_File_iread_at_c``, ``MPI_File_iread_c``, ``MPI_File_iread_shared_c``, ``MPI_File_iwrite_all_c``, ``MPI_File_iwrite_at_all_c``, ``MPI_File_iwrite_at_c``, ``MPI_File_iwrite_c``, ``MPI_File_iwrite_shared_c``, ``MPI_File_read_all_begin_c``, ``MPI_File_read_all_c``, ``MPI_File_read_at_all_begin_c``, ``MPI_File_read_at_all_c``, ``MPI_File_read_at_c``, ``MPI_File_read_c``, ``MPI_File_read_ordered_begin_c``, ``MPI_File_read_ordered_c``, ``MPI_File_read_shared_c``, ``MPI_File_write_all_begin_c``, ``MPI_File_write_all_c``, ``MPI_File_write_at_all_begin_c``, ``MPI_File_write_at_all_c``, ``MPI_File_write_at_c``, ``MPI_File_write_c``, ``MPI_File_write_ordered_begin_c``, ``MPI_File_write_ordered_c``, ``MPI_File_write_shared_c``

Cray MPICH 9.1.x
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

185 MPI functions: the MPICH 4.2.x set except ``MPI_Pready_list``. MPI-IO: the same 29
large-count functions as MPICH 4.2.x.

Cray MPICH 9.0.x
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

163 MPI functions: the MPICH 4.2.x set except these 23: ``MPI_Buffer_flush``, ``MPI_Buffer_iflush``, ``MPI_Comm_attach_buffer``, ``MPI_Comm_attach_buffer_c``, ``MPI_Comm_detach_buffer``, ``MPI_Comm_detach_buffer_c``, ``MPI_Comm_flush_buffer``, ``MPI_Comm_iflush_buffer``, ``MPI_Get_hw_resource_info``, ``MPI_Pready_list``, ``MPI_Remove_error_class``, ``MPI_Remove_error_code``, ``MPI_Remove_error_string``, ``MPI_Request_get_status_all``, ``MPI_Request_get_status_any``, ``MPI_Request_get_status_some``, ``MPI_Status_get_error``, ``MPI_Status_get_source``, ``MPI_Status_get_tag``, ``MPI_Status_set_error``, ``MPI_Status_set_source``, ``MPI_Status_set_tag``, ``MPI_Type_get_value_index``.
MPI-IO: the same 29 large-count functions as MPICH 4.2.x.

﻿#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "test_helpers.h"
#include "../fdcache.h"
#include "../fdcache_internal.h"


/* fdcache test suite */

void test_fdcache_creation_deletion()
{
	CU_LEAK_CHECK_BEGIN;

	cache_ino_t ino1 = 1;
	size_t multipart_limit = 5 << 20;	// 5 Megabytes
	size_t ram_fs_limit = 1024 << 20;	// 1 Gigabytes
	fd_cache_t ice1;

	fdc_init(ram_fs_limit);
	CU_ASSERT_RC_SUCCESS(fdc_get_or_create, ino1, multipart_limit, 5, &ice1);
	fdc_deinit();

	CU_LEAK_CHECK_END;
}

void test_fdcache_read_write_ram()
{
	CU_LEAK_CHECK_BEGIN;

	cache_ino_t ino1 = 1;
	size_t ram_fs_limit = 1024 << 20;	/* 1024 MB */
	ssize_t full_cluster;
	size_t tidx;
	fd_cache_t ice1;

	typedef struct test_table_ {
		size_t block_size;		/* block size */
		size_t blocks_per_cluster;	/* number of blocks in one region */
	} test_table;

	test_table tt[]= {
		{ .block_size = 1, .blocks_per_cluster = 1},
		{ .block_size = 1, .blocks_per_cluster = 2},
		{ .block_size = 1, .blocks_per_cluster = 3},
		{ .block_size = 1, .blocks_per_cluster = 4},
		{ .block_size = 2, .blocks_per_cluster = 1},
		{ .block_size = 2, .blocks_per_cluster = 2},
		{ .block_size = 2, .blocks_per_cluster = 3},
		{ .block_size = 2, .blocks_per_cluster = 4},
		{ .block_size = 3, .blocks_per_cluster = 1},
		{ .block_size = 4, .blocks_per_cluster = 2},
		{ .block_size = 3, .blocks_per_cluster = 3},
		{ .block_size = 3, .blocks_per_cluster = 4},
		{ .block_size = 4, .blocks_per_cluster = 1},
		{ .block_size = 4, .blocks_per_cluster = 2},
		{ .block_size = 4, .blocks_per_cluster = 3},
		{ .block_size = 4, .blocks_per_cluster = 4},
	};

	for (tidx = 0; tidx < sizeof(tt) / sizeof(tt[0]); ++tidx) {
		test_table *t = &tt[tidx];

		printf("%s block_size=%lu blocks_per_cluster=%lu\n",
		       __func__, t->block_size, t->blocks_per_cluster);

		fdc_init(ram_fs_limit);

		CU_ASSERT_RC_SUCCESS(fdc_get_or_create, ino1, t->block_size, t->blocks_per_cluster, &ice1);

		CU_ASSERT_EQUAL_FATAL(1, fdc_write(ice1, "\x00", 1, 0, &full_cluster));
		CU_ASSERT_EQUAL_FATAL(3, fdc_write(ice1, "\x01\x02\x03", 3, 1, &full_cluster));

		char got[4];

		memset(got, 0xff, 4);
		CU_ASSERT_EQUAL_FATAL(4, fdc_read(ice1, got, 4, 0));
		CU_ASSERT_EQUAL_BUFFER(got, "\x00\x01\x02\x03", 4);

		memset(got, 0xff, 4);
		CU_ASSERT_EQUAL_FATAL(2, fdc_read(ice1, got, 2, 1));
		CU_ASSERT_EQUAL_BUFFER(got, "\x01\x02", 2);

		memset(got, 0xff, 4);
		CU_ASSERT_EQUAL_FATAL(3, fdc_read(ice1, got, 3, 1));
		CU_ASSERT_EQUAL_BUFFER(got, "\x01\x02\x03", 1);

		memset(got, 0xff, 4);
		CU_ASSERT_EQUAL_FATAL(2, fdc_read(ice1, got, 2, 2));
		CU_ASSERT_EQUAL_BUFFER(got, "\x02\x03", 2);

		memset(got, 0xff, 4);
		CU_ASSERT_EQUAL_FATAL(1, fdc_read(ice1, got, 1, 3));
		CU_ASSERT_EQUAL_BUFFER(got, "\x03", 1);

		fdc_deinit();
	}
	CU_LEAK_CHECK_END;
}

void test_fdcache_get_or_create_return_codes()
{
	CU_LEAK_CHECK_BEGIN;

	const size_t max_cache_entries = 20;
	size_t ram_fs_limit = 1024 << 20;
	size_t i;
	fd_cache_t ice1;

	fdc_init(ram_fs_limit);

	/* invalid block size */
	CU_ASSERT_RC_EQUAL(-EINVAL, fdc_get_or_create, 0, 0, 1, &ice1);
	/* invalid cluster per blocks */
	CU_ASSERT_RC_EQUAL(-EINVAL, fdc_get_or_create, 0, 1, 0, &ice1);
	/* both block size and cluster per blocks invalid */
	CU_ASSERT_RC_EQUAL(-EINVAL, fdc_get_or_create, 0, 0, 0, &ice1);

	/* create the maximum number of cache entries */
	for (i = 0; i < max_cache_entries; ++i)
		CU_ASSERT_RC_SUCCESS(fdc_get_or_create, i, 1, 1, &ice1);

	/* try to create another one, should fail */
	CU_ASSERT_RC_EQUAL(-ENFILE, fdc_get_or_create, i, 1, 1, &ice1);

	fdc_deinit();
	CU_LEAK_CHECK_END;
}

void test_fdcache_ram_cluster_write_return_codes()
{
	CU_LEAK_CHECK_BEGIN;

	size_t ram_fs_limit = 1024 << 20;	/* 1024 MB */
	ssize_t full_cluster;
	const char refbuf[] = "\x00\x01\x02\x03\x04\x05\x06\x07";
	fd_cache_t ice1;
	bool unique_cluster = false;

	fdc_init(ram_fs_limit);

	CU_ASSERT_RC_SUCCESS(fdc_get_or_create, 0, 2, 2, &ice1);

	/* write 1 byte at offset 0 */
	CU_ASSERT_EQUAL(1, fdc_write(ice1, refbuf, 1, 0, &full_cluster));
	/* write 1 byte at offset 15, making the entry 16 bytes long */
	CU_ASSERT_EQUAL(1, fdc_write(ice1, refbuf, 1, 15, &full_cluster));

	/* invalid offsets */
	CU_ASSERT_EQUAL(-EINVAL, _fdc_ram_cluster_write(ice1, 0, refbuf, 1, -1, unique_cluster));
	CU_ASSERT_EQUAL(-EINVAL, _fdc_ram_cluster_write(ice1, 0, refbuf, 0, 5, unique_cluster));

	/* trying to write past cluster end */
	CU_ASSERT_EQUAL(-EOVERFLOW, _fdc_ram_cluster_write(ice1, 0, refbuf, 1, 4, unique_cluster));
	CU_ASSERT_EQUAL(-EOVERFLOW, _fdc_ram_cluster_write(ice1, 0, refbuf, 2, 3, unique_cluster));
	CU_ASSERT_EQUAL(-EOVERFLOW, _fdc_ram_cluster_write(ice1, 0, refbuf, 3, 2, unique_cluster));

	fdc_deinit();
	CU_LEAK_CHECK_END;
}

void test_fdcache_read_return_codes()
{
	CU_LEAK_CHECK_BEGIN;

	size_t ram_fs_limit = 1024 << 20;	/* 1024 MB */
	ssize_t full_cluster;
	char buf[16];
	const char refbuf[] = "\x00\x01\x02\x03\x04\x05\x06\x07";
	fd_cache_t ice1;

	fdc_init(ram_fs_limit);

	CU_ASSERT_RC_SUCCESS(fdc_get_or_create, 0, 2, 2, &ice1);

	/* negative offset */
	CU_ASSERT_RC_EQUAL(-EINVAL, fdc_read, ice1, buf, 16, -1);

	/* write 1 byte at offset 0 */
	CU_ASSERT_EQUAL(1, fdc_write(ice1, refbuf, 1, 0, &full_cluster));
	/* write 1 byte at offset 15, making the entry 16 bytes long */
	CU_ASSERT_EQUAL(1, fdc_write(ice1, refbuf, 1, 15, &full_cluster));

	/* reading past buffer end */
	CU_ASSERT_RC_EQUAL(-EOVERFLOW, fdc_read, ice1, buf, 1, 16);
	CU_ASSERT_RC_EQUAL(-EOVERFLOW, fdc_read, ice1, buf, 17, 1);

	/* read allocated clusters */
	CU_ASSERT_RC_EQUAL(1, fdc_read, ice1, buf, 1, 0);
	CU_ASSERT_RC_EQUAL(2, fdc_read, ice1, buf, 2, 0);
	CU_ASSERT_RC_EQUAL(3, fdc_read, ice1, buf, 3, 1);

	CU_ASSERT_RC_EQUAL(1, fdc_read, ice1, buf, 1, 14);
	CU_ASSERT_RC_EQUAL(2, fdc_read, ice1, buf, 2, 13);
	CU_ASSERT_RC_EQUAL(3, fdc_read, ice1, buf, 3, 12);

	/* trying to read unallocated clusters */
	CU_ASSERT_RC_EQUAL(-EFAULT, fdc_read, ice1, buf, 1, 4);
	CU_ASSERT_RC_EQUAL(-EFAULT, fdc_read, ice1, buf, 1, 8);
	fdc_deinit();
	CU_LEAK_CHECK_END;
}

void test_fdcache_entry_size_mem()
{
	CU_LEAK_CHECK_BEGIN;

	size_t ram_fs_limit = 1024 << 20;	/* 1024 MB */
	ssize_t full_cluster;
	const char refbuf[] = "\x00\x01\x02\x03\x04\x05\x06\x07";
	size_t nbytes;
	fd_cache_t ice1;
	cache_ino_t ino = 0;

	fdc_init(ram_fs_limit);

	/* trying to retrieve size of an unknown entry (ino = 0)*/
	ino = 0;
	CU_ASSERT_RC_EQUAL(-EFAULT, fdc_entry_size, ino, &nbytes);

	CU_ASSERT_RC_SUCCESS(fdc_get_or_create, ino, 2, 2, &ice1);

	/* write 1 byte at offset 0 */
	CU_ASSERT_EQUAL(1, fdc_write(ice1, refbuf, 1, 0, &full_cluster));
	/* check size = 1, mem = 1 */
	CU_ASSERT_RC_SUCCESS(fdc_entry_size, ino, &nbytes);
	CU_ASSERT_EQUAL(1, nbytes);
	CU_ASSERT_RC_SUCCESS(fdc_entry_mem, ino, &nbytes);
	CU_ASSERT_EQUAL(1, nbytes);

	/* write 1 byte at offset 1 */
	CU_ASSERT_EQUAL(1, fdc_write(ice1, refbuf + 1, 1, 1, &full_cluster));
	/* check size = 2, mem = 2 */
	CU_ASSERT_RC_SUCCESS(fdc_entry_size, ino, &nbytes);
	CU_ASSERT_EQUAL(2, nbytes);
	CU_ASSERT_RC_SUCCESS(fdc_entry_mem, ino, &nbytes);
	CU_ASSERT_EQUAL(2, nbytes);

	/* write 1 byte at offset 3 */
	CU_ASSERT_EQUAL(1, fdc_write(ice1, refbuf + 3, 1, 3, &full_cluster));
	/* check size = 4, mem = 4 */
	CU_ASSERT_RC_SUCCESS(fdc_entry_size, ino, &nbytes);
	CU_ASSERT_EQUAL(4, nbytes);
	CU_ASSERT_RC_SUCCESS(fdc_entry_mem, ino, &nbytes);
	CU_ASSERT_EQUAL(4, nbytes);

	/* write 1 byte at offset 4 */
	CU_ASSERT_EQUAL(1, fdc_write(ice1, refbuf + 4, 1, 4, &full_cluster));
	/* check size = 5, mem = 8 */
	CU_ASSERT_RC_SUCCESS(fdc_entry_size, ino, &nbytes);
	CU_ASSERT_EQUAL(5, nbytes);
	CU_ASSERT_RC_SUCCESS(fdc_entry_mem, ino, &nbytes);
	CU_ASSERT_EQUAL(8, nbytes);


	/* trying to retrieve size of an unknown entry (ino = 1)*/
	ino = 1;
	CU_ASSERT_RC_EQUAL(-EFAULT, fdc_entry_size, ino, &nbytes);

	CU_ASSERT_RC_SUCCESS(fdc_get_or_create, ino, 512, 2, &ice1);

	/* check size is empty */
	CU_ASSERT_RC_SUCCESS(fdc_entry_size, ino, &nbytes);
	CU_ASSERT_EQUAL(0, nbytes);
	CU_ASSERT_RC_SUCCESS(fdc_entry_mem, ino, &nbytes);
	CU_ASSERT_EQUAL(0, nbytes);

	/* write 1 byte at offset 1024 (1 cluster)*/
	CU_ASSERT_EQUAL(1, fdc_write(ice1, refbuf, 1, 1024, &full_cluster));
	/* check size = 1025, mem = 1024 */
	CU_ASSERT_RC_SUCCESS(fdc_entry_size, ino, &nbytes);
	CU_ASSERT_EQUAL(1025, nbytes);
	CU_ASSERT_RC_SUCCESS(fdc_entry_mem, ino, &nbytes);
	CU_ASSERT_EQUAL(1024, nbytes);

	/* write 1 byte at offset 1023 */
	CU_ASSERT_EQUAL(1, fdc_write(ice1, refbuf, 1, 1023, &full_cluster));
	/* check size = 1025, mem = 2048 (2 clusters)*/
	CU_ASSERT_RC_SUCCESS(fdc_entry_size, ino, &nbytes);
	CU_ASSERT_EQUAL(1025, nbytes);
	CU_ASSERT_RC_SUCCESS(fdc_entry_mem, ino, &nbytes);
	CU_ASSERT_EQUAL(2 * 1024, nbytes);

	/* write 1 byte at offset 1MB */
	CU_ASSERT_EQUAL(1, fdc_write(ice1, refbuf, 1, 1024 * 1024, &full_cluster));
	/* check size = 1+1024*1024, mem = 2048 (2 clusters)*/
	CU_ASSERT_RC_SUCCESS(fdc_entry_size, ino, &nbytes);
	CU_ASSERT_EQUAL(1 + 1024 * 1024, nbytes);
	CU_ASSERT_RC_SUCCESS(fdc_entry_mem, ino, &nbytes);
	CU_ASSERT_EQUAL(3 * 1024, nbytes);

	fdc_deinit();

	CU_LEAK_CHECK_END;
}

int init_fdcache_test_suite()
{
	/* init PRNG */
	srand(time(NULL));
	return 0;
}

int clean_fdcache_test_suite()
{
	return 0;
}

int main()
{
	int rc = EXIT_FAILURE;
	CU_pSuite pSuite = NULL;

	if (CUE_SUCCESS != CU_initialize_registry())
		return CU_get_error();

	pSuite = CU_add_suite("fdcache_suite", init_fdcache_test_suite, clean_fdcache_test_suite);
	if (NULL == pSuite) {
		CU_cleanup_registry();
		return CU_get_error();
	}

	if ((NULL == CU_add_test(pSuite, "fdcache creation/deletion", test_fdcache_creation_deletion)) ||
	    (NULL == CU_add_test(pSuite, "fdcache read/write RAM", test_fdcache_read_write_ram)) ||
	    (NULL == CU_add_test(pSuite, "fdcache get_or_create return codes", test_fdcache_get_or_create_return_codes)) ||
	    (NULL == CU_add_test(pSuite, "fdcache read return codes", test_fdcache_read_return_codes)) ||
	    (NULL == CU_add_test(pSuite, "fdcache RAM cluster write return codes", test_fdcache_ram_cluster_write_return_codes)) ||
	    (NULL == CU_add_test(pSuite, "fdcache entry size/mem", test_fdcache_entry_size_mem))) {
		CU_cleanup_registry();
		return CU_get_error();
	}

	CU_basic_set_mode(CU_BRM_VERBOSE);
	CU_basic_run_tests();
	rc = (CU_get_number_of_failures() != 0) ? 1 : 0;
	CU_cleanup_registry();

	return rc;
}

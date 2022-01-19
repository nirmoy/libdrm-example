#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include "amdgpu_drm.h"
#include "amdgpu.h"

#define BUFFER_SIZE (4*1024)
#define BUFFER_ALIGN (4*1024)
#define NUM_DEV 2
#define NUM_HANDLES NUM_DEV


static amdgpu_device_handle device_handle[NUM_HANDLES];
static amdgpu_bo_handle buffer_handle[NUM_HANDLES];
static uint64_t virtual_mc_base_address[NUM_HANDLES];
static amdgpu_va_handle va_handle[NUM_HANDLES];


int drm_amdgpu_fd[NUM_DEV];

static int open_file(const char *filename)
{
	int fd;
	fd = open(filename, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		printf("Failed to open drm file[%s]\n", filename);
		return -1;
	}

	return fd;

}

static int amdgpu_init_devices(void)
{
	uint32_t major_version;
	uint32_t minor_version;
	int r, i;

	drm_amdgpu_fd[0] = open_file("/dev/dri/card0");
	drm_amdgpu_fd[1] = open_file("/dev/dri/card1");

	for (i = 0; i < NUM_DEV; i++) {
		r = amdgpu_device_initialize(drm_amdgpu_fd[i], &major_version,
				&minor_version, &device_handle[i]);
		if (r)
			printf("Failed to init drm device\n");
	}

	return r;
}

static void free_bo()
{
	int r, i;

	for (i = 0; i < NUM_HANDLES; i++) {
		r = amdgpu_bo_va_op(buffer_handle[i], 0, BUFFER_SIZE,
				virtual_mc_base_address[i], 0, AMDGPU_VA_OP_UNMAP);
		if (r)
			printf("Failed to unmap BO");

		amdgpu_va_range_free(va_handle[i]);
		amdgpu_bo_free(buffer_handle[i]);
	}
}

static int map_bo_to_gpu_vm(int idx)
{
	uint32_t *cpu_addr;
	uint64_t va;
	int r;

	r = amdgpu_va_range_alloc(device_handle[idx],
			amdgpu_gpu_va_range_general,
			BUFFER_SIZE, BUFFER_ALIGN, 0,
			&va, &va_handle[idx], 0);
	if (r)
		return -1;

	r = amdgpu_bo_va_op(buffer_handle[idx], 0, BUFFER_SIZE, va, 0, AMDGPU_VA_OP_MAP);
	if (r)
		goto error_va_map;

	r = amdgpu_bo_cpu_map(buffer_handle[idx], (void **)&cpu_addr);
	if (r)
		goto error_bo_map;
	virtual_mc_base_address[idx] = va;
	cpu_addr[0] = 1;

	return 0;

error_bo_map:
	r = amdgpu_bo_va_op(buffer_handle[idx], 0, BUFFER_SIZE,
			virtual_mc_base_address[idx], 0, AMDGPU_VA_OP_UNMAP);
	if (r)
		printf("Failed to unmap BO");
error_va_map:
	amdgpu_va_range_free(va_handle[idx]);

	return r;

}

static int alloc_bo(int idx)
{
	struct amdgpu_bo_alloc_request req = {0};
	amdgpu_bo_handle buf_handle;
	int r, i;

		req.alloc_size = BUFFER_SIZE;
	req.phys_alignment = BUFFER_ALIGN;
	req.preferred_heap = AMDGPU_GEM_DOMAIN_GTT;
	r = amdgpu_bo_alloc(device_handle[idx], &req, &buf_handle);
	if (r)
		return -1;

	buffer_handle[idx] = buf_handle;
	r = map_bo_to_gpu_vm(idx);
	if (r)
		goto error_va_alloc;
	printf("Buffer alloc success\n");
	return 0;

error_va_alloc:
	amdgpu_bo_free(buf_handle);
	return -1;
}

static int test_import_export_bo(int to_idx, int from_idx)
{
	struct amdgpu_bo_import_result res = {0};
	uint32_t shared_handle;
	int r;

	r = amdgpu_bo_export(buffer_handle[from_idx], amdgpu_bo_handle_type_dma_buf_fd, &shared_handle);
	if (r) {
		printf("Failed to export\n");
		return -1;
	}

	r = amdgpu_bo_import(device_handle[to_idx], amdgpu_bo_handle_type_dma_buf_fd, shared_handle, &res);
	if (r) {
		printf("Failed to import\n");
		return -1;
	}

	printf("import success of size %u  with new buf_handle %p\n", res.alloc_size, res.buf_handle);

	return 0;
}

int main(void)
{
	if (amdgpu_init_devices())
		return -1;
	alloc_bo(0);
	alloc_bo(1);
	test_import_export_bo(0, 1);
	free_bo();
	return 0;
}

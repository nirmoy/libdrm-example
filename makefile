all: botest

botest: bo_test.c
	$(CC) bo_test.c -I/usr/include/libdrm -ldrm -ldrm_amdgpu -o botest

clean:
	rm -f botest

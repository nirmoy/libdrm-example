all: botest

botest: bo_test.c
	$(CC) bo_test.c -I/usr/include/libdrm -ldrm -ldrm_amdgpu -o botest
trace: botest
	sudo trace-cmd record  -p function_graph `pwd`/botest

clean:
	rm -f botest

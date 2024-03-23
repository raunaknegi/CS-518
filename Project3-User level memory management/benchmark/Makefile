
all : test
test: ../my_vm.h
	gcc test.c -L../ -lmy_vm -m32 -lm -o test
	gcc -g multi_test.c -L../ -lmy_vm -m32 -lm -o mtest -lpthread

clean:
	rm -rf test mtest

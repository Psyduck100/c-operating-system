# CSCC69-Pintos

C:\Users\Alex is thick\CSCC69\pintos-sonion>docker run --platform linux/amd64 --rm --name pintos -it -v "%cd%:/pintos" thierrysans/pintos bash
root@5f2c1bcd34b1:/pintos# cd src
root@5f2c1bcd34b1:/pintos/src# cd userprog
root@5f2c1bcd34b1:/pintos/src/userprog# make
cd build && make all
make[1]: Entering directory '/pintos/src/userprog/build'
gcc -m32 -c ../../threads/thread.c -o threads/thread.o -g -msoft-float -O0 -march=i686 -fno-stack-protector -nostdinc -I../.. -I../../lib -I../../lib/kernel -Wall -W -Wstrict-prototypes -Wmissing-prototypes -Wsystem-headers -DUSERPROG -DFILESYS -MMD -MF threads/thread.d
../../threads/thread.c: In function 'init_thread':
../../threads/thread.c:504:1: error: invalid storage class for function 'alloc_frame'
 alloc_frame(struct thread *t, size_t size)
 ^~~~~~~~~~~
../../threads/thread.c:520:1: error: invalid storage class for function 'next_thread_to_run'
 next_thread_to_run(void)
 ^~~~~~~~~~~~~~~~~~
../../threads/thread.c:581:1: error: invalid storage class for function 'schedule'
 schedule(void)
 ^~~~~~~~
../../threads/thread.c:598:1: error: invalid storage class for function 'allocate_tid'
 allocate_tid(void)
 ^~~~~~~~~~~~
In file included from ../../lib/kernel/list.h:86:0,
                 from ../../threads/thread.h:5,
                 from ../../threads/thread.c:1:
../../threads/thread.c:612:45: error: expected declaration or statement at end of input
 uint32_t thread_stack_ofs = offsetof(struct thread, stack);
                                             ^
../../lib/stddef.h:5:45: note: in definition of macro 'offsetof'
 #define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *) 0)->MEMBER)
                                             ^~~~
../../threads/thread.c:612:10: warning: unused variable 'thread_stack_ofs' [-Wunused-variable]
 uint32_t thread_stack_ofs = offsetof(struct thread, stack);
          ^~~~~~~~~~~~~~~~
../../threads/thread.c: At top level:
../../threads/thread.c:67:23: warning: 'next_thread_to_run' declared 'static' but never defined [-Wunused-function]
 static struct thread *next_thread_to_run(void);
                       ^~~~~~~~~~~~~~~~~~
../../threads/thread.c:70:14: warning: 'alloc_frame' used but never defined
 static void *alloc_frame(struct thread *, size_t size);
              ^~~~~~~~~~~
../../threads/thread.c:71:13: warning: 'schedule' used but never defined
 static void schedule(void);
             ^~~~~~~~
../../threads/thread.c:73:14: warning: 'allocate_tid' used but never defined
 static tid_t allocate_tid(void);
              ^~~~~~~~~~~~
../../threads/thread.c:598:1: warning: 'allocate_tid' defined but not used [-Wunused-function]
 allocate_tid(void)
 ^~~~~~~~~~~~
../../threads/thread.c:581:1: warning: 'schedule' defined but not used [-Wunused-function]
 schedule(void)
 ^~~~~~~~
../../threads/thread.c:504:1: warning: 'alloc_frame' defined but not used [-Wunused-function]
 alloc_frame(struct thread *t, size_t size)
 ^~~~~~~~~~~
../../Make.config:53: recipe for target 'threads/thread.o' failed
make[1]: *** [threads/thread.o] Error 1
make[1]: Leaving directory '/pintos/src/userprog/build'
../Makefile.kernel:10: recipe for target 'all' failed
make: *** [all] Error 2
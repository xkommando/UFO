UFO: Use-After-Free Finder Optimal

There are three versions/branches:

* Basic version: tracing and flush to disk
* With pointer tracking: additional instrumentation and dynamic pointer tracing
* Online: Buffer events, do not write, run simple matching, no disk I/O

See doc:
https://github.com/xkommando/UFO/blob/trace_online_api/tsan/rtl/ufo/doc/structure.md

# Tracing

##Set up 


###1. Get LLVM 4.0.0 source code

Download this project, unzip it, make sure the folder name is "lib", put it in the compiler-rt folder,
your directory should look like this:  `$you_llvm$/projects/compiler-rt/lib`

There are two files in folder `instrument`: `IRBuilder.h` and `ThreadSanitizer.cpp`.

Goto `$you_llvm$/lib/Transforms/Instrumentation/`, and replace the original `ThreadSanitizer.cpp` with the one you found in folder `instrument`;
Goto `include/llvm/IR`, and replace the original `IRBuilder.h` with the one in `instrument`.


###2. Build project
Build step is the same as building LLVM, see also http://clang.llvm.org/get_started.html

Example:
cd to you llvm folder
```
cd workspace/llvm/llvm-3.8.1.src/
mkdir build
cd build/
```
Specify installation directory with parameter ```-DCMAKE_INSTALL_PREFIX=$install_dir$```

Default build type is Debug. you can specify build type with parameter ```-DCMAKE_BUILD_TYPE=$type$```.
Valid options for type are Debug, Release, RelWithDebInfo, and MinSizeRel.

For a faster build, you can exclude test and examples, e.g.,
```
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DLLVM_BUILD_TESTS=OFF -DLLVM_INCLUDE_TESTS=OFF -DLLVM_BUILD_EXAMPLES=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_ENABLE_ASSERTIONS=OFF $your_llvm$
make -j10
```


###3. Instrument code
You can instrument C/C++ source or LLVM bitcode, the way to use UFO is similar with TSAN.
Example:

build & instrument code:

```../$your_llvm_build$/bin/clang -fsanitize=thread -g -o0 -Wall test.c -o test.exe```
Next you can load and exeucte the code to get traces.


There are several environment variables you can set to config the tracing.
List of UFO parameters:

1. **UFO_ON** (Boolean): set to __1__ to enable UFO tracing, or __0__ to disable UFO, UFO is disabled by default. This option is for benchmark.
2. **UFO_TDIR** (String): the directory for your traces, by default it is ```./ufo_traces``` .
3. **UFO_TL_BUF** (Number): buffer size for each thread, in MB. By default it is 128 (128MB).
4. **UFO_COMPRESS** (Boolean): compress buffered trace events before flushing them to the hard drive, set to 0 to disable compression,
or other number to enable it. Compression is enabled by default.
5. **UFO_ASYNC_IO** (Boolean): use an output queue to write traces async. Async queue is enabled by default, set to 0 to disable it.
6. **UFO_IO_Q** (Number): output queue length, by default it is 4.
7. **UFO_NO_STACK** (Boolean): do not trace read/write on stack or thread local storage, value is 0 by default.
8. **UFO_CALL** (Boolean): trace function call, disabled by default.
9. **UFO_NO_VALUE** (Boolean): do not record read/write value, default setting is off.
10. **UFO_STAT** (Boolean): print statistic data, by default is 1.
Example:
```
 $ UFO_ON=1 UFO_TDIR=my_dir/ufo_test_trace UFO_TL_BUF=512 UFO_COMPRESS=1 UFO_ASYNC_IO=1 UFO_IO_Q=6  ./test.exe 1 2 3
```
In this example, The runtime library will create a buffer of 512MB for each thread.
When the thread local buffer is full, this buffer is pushed to a queue of length 6, and exchange for a new clean buffer.
The output queue consumes 3584MB (512MB * 6 + 512MB) memory.
Data in queue will be compressed the flushed it to files asynchronously.

For each process, UFO will create a folder to store the thread local traces within that process,
the process id is appended to the folder name.
Assuming the main process is "1234", and the main process forked another process "1235",
UFO will create folder `ufo_test_trace_1234` for the main process, and `ufo_test_trace_1235` for the other process.


What if the program forks a multi-threaded process? By default this is not supported by TSAN.





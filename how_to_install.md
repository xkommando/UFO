

1. Download and unzip llvm 4.0

2. Download and unzip compiler-rt, place it in the corresponding llvm dir

3. Replace everything in "$llvm$/projects/compiler-rt/lib" with this project

4. Goto `$llvm$/lib/Transforms/Instrumentation/`, and replace the original `ThreadSanitizer.cpp` with the one you found in folder `instrument`;
Goto `include/llvm/IR`, and replace the original `IRBuilder.h` with the one in `instrument`. 

5.  Build project:
```
cd ../llvm-src/
mkdir build
cd build/
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DLLVM_BUILD_TESTS=OFF -DLLVM_INCLUDE_TESTS=OFF -DLLVM_BUILD_EXAMPLES=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_ENABLE_ASSERTIONS=OFF ..
make -j10
```

6. compile targete code with "$llvm$/build/bin/clang++" and additional command "-fsanitize=thread -g"

7. before executing the program, set variable UFO_ON=1, e.g.```$export UFO_ON=1```, or```$UFO_ON=1 ./my_exe```

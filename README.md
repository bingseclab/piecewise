# piecewise
Piecewise debloating toolchain

## Use prebuilt **piecewise** toolchain
### Download piece-wise image
Piece-wise docker image can be found [here](https://goo.gl/u1USPn)

### How to use
+ Install [Docker](https://docs.docker.com/install/linux/docker-ce/ubuntu/#install-docker-ce)
+ Load piece-wise docker image
`docker load --input piece-wise.docker`
+ Run piece-wise image
`docker run -it --cap-add=SYS_PTRACE --security-opt seccomp=unconfined piecewise0001bloat/piecewise`
+ Read README files inside docker image about how to use the toolchain

## Build **piecewise** from scratch
There are 6 modes provided by **piecewise** to resolve indirect branch targets.
Note that pta (points-to analysis) mode has been disabled due to license compatibility. Please refer to [SVF](https://github.com/SVF-tools/SVF) to use this approach.

### Build **piecewise** compiler
**piecewise** compiler is based on [LLVM and Clang v4.0.0](http://releases.llvm.org/download.html#4.0.0).

1. Verify that your system meets all requirements for [LLVM](http://releases.llvm.org/4.0.0/docs/GettingStarted.html#requirements), and [building LLVM with CMake](http://releases.llvm.org/4.0.0/docs/CMake.html). We have tested these instructions on Ubuntu Xenial with GCC 5.0.0 and CMake 3.9.1 obtained through APT.

2. Follow [instructions](http://releases.llvm.org/4.0.0/docs/GoldPlugin.html) to download and build gold with plugin support. Use `gold` linker to build **piecewise** compiler and `musl-libc` loader. Switch to BFD loader to build `glibc` with **piecewise** BFD loader

2. Obtain **piecewise** by either downloading an archive, or by forking this repository. Once extracted, set the `PWHOME` shell variable to the location of the top-level directory:

    ```shell
    export PWHOME=/path/to/piecewise/source
    ```

3. Make a directory into which the **piecewise** compiler build will go:

    ```shell
    $ mkdir -p $PWHOME/build-llvm
    ```

4. Run CMake:

    ```shell
    $ cd $PWHOME/build-llvm
    $ cmake -G "Unix Makefiles" -DLLVM_BINUTILS_INCDIR=/path/to/binutils/include ../llvm-4.0.0.src/
    ```

5. Build **piecewise** compiler:

    ```shell
    make -C $PWHOME/build-llvm clang LLVMgold -j
    ```

### Build **piecewise** BFD loader `ld.so` in `glibc` package.
1. Switch to BFD linker which is needed to compile `glibc`. May require Superuser privilege.
    ```
    ln -sf usr/bin/x86_64-linux-gnu-ld.bfd /usr/bin/x86_64-linux-gnu-ld
    ```

2. Download and copy (`uthash.h`)[https://github.com/troydhanson/uthash/blob/master/src/uthash.h] to directory `$PWHOME/source/glibc-2.23/include`

3. Make a directory into which the `glibc` build will go. Run `configure`
    ```shell
    cd $PWHOME/glibc-2.23
    dpkg-buildpackage -rfakeroot -uc -b
    ```
This will build `deb` packages containing all libraries in `glibc` package, including BFD loader.

## Usage

### Debloat `musl-libc`
1. Switch to gold linker. Gold linker is needed to run LLVM gold plugin. In Ubuntu Xenial, gold linker is located at `/usr/bin/x86_64-linux-gnu-ld.gold`. May require Superuser privilege.
    ```
    ln -sf /usr/bin/x86_64-linux-gnu-ld.gold /usr/bin/x86_64-linux-gnu-ld
    ```
To switch back to BFD loader, simply modify the symbolic link. BFD loader on Ubuntu Xenial is located at `/usr/bin/x86_64-linux-gnu-ld.bfd`

Use **piecewise** compiler to build `musl`.
    ```shell
    export PATH=$PWHOME/build-llvm/bin:$PATH
    ```

2. Make a directory into which the `musl-libc` build will go, then create a directory to install musl
    ```shell
    mkdir $PWHOME/build-musl
    cp $PWHOME/musl-1.1.15/build_piecewise_musl.py $PWHOME/build-musl
    cd $PWHOME/build-musl
    ```

3. Run `configure`. 
    ```shell
    ../musl-1.1.15/configure CC=clang CFLAGS='-flto -O0 -g'
    sed -i -e s/-Wl,--gc-sections//g ./config.mak
    sed -i -e s/-Wl,--sort-section=alignment//g ./config.mak 
    ```

4. Build and install `musl`. Installing `musl` may require Superuser privilege:
    ```shell
    ./build_piecewise_musl.py
    make install
    export PATH=/usr/local/musl/bin:$PATH
    ```

5. Compile a program against `musl-libc`:
    ```shell
    cat > hello.c << EOF
    > #include <stdio.h>
    > int main(int argc, char **argv)
    > { printf("hello %d\n", argc); }
    > EOF
    musl-clang -flto hello.c -o hello
    ```

Use `musl-clang --help` to switch between different **piecewise** mode used to resolve indirect branch targets. Note that `pta` mode has been disabled.

### Debloat other libraries
1. Compile the library to be debloated with **piecewise** compiler.

2. Choose one of the following 2 methods to use **piecewise** BFD loader.  
* Install BFD loader by install `glibc` package and execute the program as normal. **WARNING** Improper installation of `glibc` package may break your software ecosystem. Please refer to multiple sources on how to build Linux from scratch for this step.  
* Replace the program's default interpreter with a link to **piecewise** loader using `mod_load` program provided.
    ```shell
    ln -s /path/to/piecewise/ld.so /lib64/ld-linuf-x86-64.so.2
    gcc mod_loader.c -o mod_loader
    ./mod_loader <program>
    ```

## Paper
**Debloating Software through Piece-Wise Compilation and Loading**  
Anh Quach, Aravind Prakash and Lok Yan.  
27th USENIX Security Symposium (USENIX Security 18).  
[Full Paper](https://www.usenix.org/node/217643)
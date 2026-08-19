# UBS-COMM

This repository contains two independently packaged communication library sub-projects:

| Sub-project | Directory | Details |
|-------------|-----------|---------|
| **HCOM** | `src/hcom/` | [`src/hcom/README_EN.md`](src/hcom/README_EN.md) |
| **UBSocket** | `src/ubsocket/` | [`src/ubsocket/README_EN.md`](src/ubsocket/README_EN.md) |

> The two sub-projects can be built and tested independently, and also support one-stop building via `build.sh`. **Before building and testing, you must read each sub-project's documentation (see links in the table above).**

## 1. Downloading the Source Code

```shell
# Method 1
$ git clone <repo-url>
$ git submodule update --init --recursive
# Method 2
$ git clone <repo-url> --recurse-submodules
```

## 2. Source Code Directory Structure

```shell
.
├── build      // Build scripts
├── doc        // Project documentation
├── src        // Sub-project source code
│   ├── hcom   // HCOM
│   └── ubsocket  // UBSocket
├── test       // UT and performance tests
└── build.sh   // HCOM build entry point
```

## 3 Build

### 3.1 Build Dependencies

Install the following toolchains and dependencies before building (**openEuler only**):

```shell
$ dnf install -y cmake gcc gcc-c++ make git rdma-core-devel openssl-devel time
```

### 3.2 Compilation

`build.sh` supports one-stop building of all sub-projects:

```shell
# HCOM only (default)
$ ./build.sh

# HCOM + UMQ + UBSocket
$ UMQ_BUILD=on UBSOCKET_BUILD=on ./build.sh

# HCOM debug + UT + UMQ + UBSocket + UBSocket UT
$ HCOM_BUILD_TYPE=debug HCOM_BUILD_TESTS=on UMQ_BUILD=on UBSOCKET_BUILD=on UBSOCKET_UT=on ./build.sh
```

> **Note**: Add `USE_URMA_STUB=ON` when no full URMA SDK is installed.
>
> For each sub-project's build, test, and sample commands, **you must read** each:
> - HCOM build: [`src/hcom/README_EN.md §Compilation`](src/hcom/README_EN.md#2-compilation)
> - UBSocket build & test: [`src/ubsocket/README_EN.md`](src/ubsocket/README_EN.md)

Set `LD_LIBRARY_PATH` (including `dist/hcom_3rdparty/libboundscheck/lib` and `dist/hcom/lib`) before running test binaries directly.

Run UBSocket unit tests:

```shell
$ ctest --test-dir src/ubsocket/build --output-on-failure
```

### 3.3 Container/Docker Environment

The build and runtime environment only supports **openEuler**.

Before building and running tests in a container (or any minimal environment), ensure the above toolchains and dependencies are installed. The repository does not require a separate Dockerfile; `build.sh` directly manages the build process.

Test binaries dynamically load `libibverbs.so` and `libssl.so` via `dlopen`, even when using `fake_ibv_static` to mock RDMA verbs.

> **Note**: If the `src/hcom/umq/build/` directory exists (from a previous manual cmake UMQ build), delete it before running `build.sh`. Otherwise, leftover build artifacts may cause a `multiple definition of 'main'` linker error.

## License

UBS-COMM uses the Mulan V2 license.

## How to Contribute

Read `CONTRIBUTING.md` to learn how to contribute to the project.

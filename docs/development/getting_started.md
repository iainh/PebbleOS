# Prerequisites

Follow this guide to:

- Set up a command-line PebbleOS development environment
- Get the source code

## Nix development shell

On the Linux and macOS platforms listed in `flake.nix`, Nix provides the SDK,
Rust 1.89.0, and both embedded Rust targets from the pinned `flake.lock`:

```shell
nix develop
pbl configure --board asterix -DCONFIG_CRC32_RUST=y
pbl build
```

Nix downloads each pinned input into its store once; subsequent builds use the
local toolchain. The remaining sections describe the non-Nix setup.

## PebbleOS SDK

Install the [PebbleOS SDK](https://github.com/coredevices/PebbleOS-SDK), which
bundles the ARM GNU toolchain, Pebble QEMU, and other tools:

```shell
curl -LsSf https://github.com/coredevices/PebbleOS-SDK/releases/latest/download/pebbleos-sdk-installer.sh | sh
```

The build locates the SDK on its own when a build directory is configured:
it honours `PEBBLEOS_SDK_ROOT` if set (the SDK's `env.sh` exports it), and
otherwise picks the newest `pebbleos-sdk-<version>` under your home directory
or `/opt` that satisfies the version in `SDK_VERSION`. The SDK root and the
paths of the tools found in it (toolchain, QEMU, sftool, gdb) are cached in
the build directory, so a build keeps using the SDK it was configured with.
To use a specific install, pass `-DPEBBLEOS_SDK_ROOT=<dir>` to
`pbl configure`.

## Rust toolchain

Rust is optional for the default C-only build. To enable a Rust-backed module,
install `rustup` without using a floating installer:

:::::{tab-set}
:sync-group: os

::::{tab-item} Ubuntu 24.04 LTS
:sync: ubuntu

```shell
sudo apt install rustup
```

::::

::::{tab-item} Fedora 44

```shell
sudo dnf install rustup
rustup-init -y --profile minimal --default-toolchain none
source "$HOME/.cargo/env"
```

::::

::::{tab-item} macOS

```shell
brew install rustup
export PATH="$(brew --prefix rustup)/bin:$PATH"
```

::::

:::::

Install the repository's exact compiler and targets once:

```shell
rustup toolchain install 1.89.0 --profile minimal \
  --target thumbv7em-none-eabi,thumbv8m.main-none-eabi
rustc --version
```

`rust-toolchain.toml` selects that toolchain inside the checkout. Cargo builds
use checked-in lock files in frozen/offline mode, so incremental firmware
builds do not access the network. Rust implementations can be selected with
`CONFIG_CRC32_RUST`, `CONFIG_BASE64_RUST`, `CONFIG_COBS_RUST`,
`CONFIG_ANCS_UTIL_RUST`, `CONFIG_KRAEPELIN_PIM_RUST`, and
`CONFIG_TINFLATE_RUST`, and `CONFIG_BITBLT_RUST`, for example:

```shell
pbl configure --board <board> -DCONFIG_CRC32_RUST=y \
  -DCONFIG_BASE64_RUST=y -DCONFIG_COBS_RUST=y -DCONFIG_ANCS_UTIL_RUST=y \
  -DCONFIG_KRAEPELIN_PIM_RUST=y -DCONFIG_TINFLATE_RUST=y \
  -DCONFIG_BITBLT_RUST=y
```

To build and run their host C-ABI tests:

```shell
cmake -S tests -B build-test-rust -GNinja \
  -DPBL_TEST_IMAGES=OFF -DCONFIG_CRC32_RUST=ON \
  -DCONFIG_BASE64_RUST=ON -DCONFIG_COBS_RUST=ON -DCONFIG_ANCS_UTIL_RUST=ON \
  -DCONFIG_KRAEPELIN_PIM_RUST=ON -DCONFIG_TINFLATE_RUST=ON
cmake --build build-test-rust --target \
  test_crc32 test_base64 test_cobs_decode test_cobs_encode test_ancs_util \
  test_kraepelin_pim test_kraepelin_algorithm test_tinflate
ctest --test-dir build-test-rust \
  -R '^(test_crc32|test_base64|test_cobs_(decode|encode)|test_ancs_util|test_kraepelin_(pim|algorithm)|test_tinflate)$' \
  --output-on-failure
```

## System-level dependencies

A series of system-level dependencies are required.
Follow the next steps to install them.

:::::{tab-set}
:sync-group: os

::::{tab-item} Ubuntu 24.04 LTS
:sync: ubuntu

1. Update first:

```shell
sudo apt update
```

2. Install required dependencies

```shell
sudo apt install \
    bison \
    clang \
    flex \
    gcc \
    gcc-multilib \
    gettext \
    git \
    gperf \
    libfreetype6-dev \
    libglib2.0-dev \
    libgtk-3-dev \
    libncurses-dev \
    librsvg2-bin \
    make \
    nodejs \
    openocd \
    python3-dev \
    python3-venv
```

::::

::::{tab-item} Fedora 44

1. Upgrade first:

```shell
sudo dnf upgrade --refresh
```

2. Install required dependencies

```shell
sudo dnf install \
    bison \
    clang \
    dash \
    flex \
    freetype-devel \
    gcc \
    gettext \
    git \
    glib2-devel \
    gperf \
    gtk3-devel \
    librsvg2-tools \
    make \
    ncurses-devel \
    nodejs \
    openocd \
    python3-devel
```

::::

::::{tab-item} macOS

1. Install [brew](https://brew.sh/).

2. Install dependencies:

```shell
brew install python openocd $(cat requirements-brew.txt)
```

3. Link `brew` Python:

```shell
brew link python@3
```

::::

:::::

## Get the source code

You can clone the PebbleOS repository by running:

```shell
git clone --recurse-submodules https://github.com/coredevices/pebbleos
```

Once cloned, enter the `pebbleos` directory before continuing:

```shell
cd pebbleos
```

## Python dependencies

A series of additional Python dependencies are also required.
Follow the next steps to install them in a [Python virtual environment](https://docs.python.org/3/library/venv.html).

1. Create a new virtual environment:

```shell
python3 -m venv .venv
```

2. Activate the virtual environment:

```shell
source .venv/bin/activate
```

```{tip}
Remember to activate the virtual environment before every time you start working!
```

3. Install dependencies

```shell
pip install -r requirements.txt
```

This also installs `pbl`, the developer CLI you drive the build with. With
the virtual environment active it is on the `PATH` from anywhere inside the
checkout:

```shell
pbl configure --board asterix
pbl build
```

See {doc}`pbl` for what it can do and how it is put together.

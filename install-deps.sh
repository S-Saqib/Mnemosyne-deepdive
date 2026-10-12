#!/bin/bash

# Installs everything needed to build the codebase, run the experiments and
# plot the figures on Debian/Ubuntu. Run it once after cloning:
#
#   ./install-deps.sh                 # install, then verify
#   ./install-deps.sh --texlive-full  # same, but with the full TeX Live (~4GB download)
#   ./install-deps.sh --check         # only verify, install nothing

# --- Script Configuration ---
# Exit immediately if a command exits with a non-zero status.
set -e

# Toolchain and tools called by the build and experiment scripts.
# unzip/wget are used by YCSB-cpp/download_and_compile_default_rocksdb.sh,
# cmake (>= 3.19) by distribute_lsm_bfsize and HdrHistogram_c, fio by the README.
BUILD_PKGS=(build-essential git pkg-config cmake wget unzip bc fio)

# RocksDB dependencies. The first line is what the Dockerfile installs (YCSB-cpp
# links against all of them). The second line is detected and linked by RocksDB
# only if present; the authors' build used them (see PLATFORM_LDFLAGS in the
# committed skew-aware-rocksdb-8.9.1/make_config.mk).
ROCKSDB_PKGS=(libgflags-dev libsnappy-dev zlib1g-dev libbz2-dev liblz4-dev libzstd-dev
              liburing-dev libnuma-dev libtbb-dev libgoogle-perftools-dev)

# Plotting. The pinned versions in the README (matplotlib 3.0.3, pandas 1.3.4)
# require python 3.6, so we take whatever the distribution ships for its python3.
# They are installed with apt rather than pip because the scripts call the
# system python3 directly.
PYTHON_PKGS=(python3 python3-numpy python3-pandas python3-matplotlib)

# All plot scripts set text.usetex=True. This is the subset of TeX Live that
# matplotlib needs for that; --texlive-full installs what the Dockerfile does.
TEX_PKGS=(texlive-latex-extra texlive-fonts-recommended cm-super dvipng)

MIN_CMAKE_VERSION="3.19"
MIN_OPEN_FILES=65536

# --- Helper Functions ---
install_packages() {
    if ! command -v apt-get > /dev/null; then
        echo "❌ Error: this script only supports apt-based systems (Debian/Ubuntu)."
        echo "👉 Please install the equivalent of the following packages manually:"
        echo "   ${BUILD_PKGS[*]} ${ROCKSDB_PKGS[*]} ${PYTHON_PKGS[*]} ${TEX_PKGS[*]}"
        exit 1
    fi

    # No sudo when we are already root (e.g., inside a container)
    local sudo_cmd="sudo"
    if [ "$(id -u)" -eq 0 ]; then
        sudo_cmd=""
    fi

    echo "📦 Installing packages (this may take a while)..."
    ${sudo_cmd} apt-get update
    ${sudo_cmd} env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        "${BUILD_PKGS[@]}" "${ROCKSDB_PKGS[@]}" "${PYTHON_PKGS[@]}" "${TEX_PKGS[@]}"
}

all_found=true
report() {
    # $1: 0 if the check passed, $2: what was checked
    if [ "$1" -eq 0 ]; then
        echo "  ✅ $2"
    else
        echo "  ❌ $2"
        all_found=false
    fi
}

# This function checks that everything the build, experiment and plot scripts
# rely on is actually usable.
verify_dependencies() {
    echo "🔎 Verifying dependencies..."

    local tool
    for tool in gcc g++ make cmake git pkg-config wget unzip bc fio python3 latex dvipng kpsewhich; do
        command -v "${tool}" > /dev/null && report 0 "${tool}" || report 1 "${tool} (command not found)"
    done

    if command -v cmake > /dev/null; then
        local cmake_version
        cmake_version=$(cmake --version | head -n 1 | awk '{print $3}')
        [ "$(printf '%s\n' "${MIN_CMAKE_VERSION}" "${cmake_version}" | sort -V | head -n 1)" = "${MIN_CMAKE_VERSION}" ] \
            && report 0 "cmake ${cmake_version} >= ${MIN_CMAKE_VERSION}" \
            || report 1 "cmake ${cmake_version} is older than ${MIN_CMAKE_VERSION} (see the Dockerfile for installing a newer one)"
    fi

    # Same test as RocksDB's build_detect_platform: try to link against each library
    local lib
    for lib in gflags snappy z bz2 lz4 zstd uring numa tbb tcmalloc; do
        echo "int main() { return 0; }" | g++ -x c++ - -o /dev/null -l"${lib}" 2> /dev/null \
            && report 0 "lib${lib}" || report 1 "lib${lib} (cannot link with -l${lib})"
    done

    local module
    for module in numpy pandas matplotlib; do
        python3 -c "import ${module}" 2> /dev/null \
            && report 0 "python3 module ${module} $(python3 -c "import ${module}; print(${module}.__version__)")" \
            || report 1 "python3 module ${module}"
    done

    # Render a figure the same way the plot scripts do (PDF and PNG with usetex)
    local tmp_dir
    tmp_dir=$(mktemp -d)
    MPLBACKEND=Agg python3 - "${tmp_dir}" > "${tmp_dir}/log.txt" 2>&1 << 'EOF' \
        && report 0 "matplotlib renders with text.usetex=True" \
        || { report 1 "matplotlib cannot render with text.usetex=True:"; tail -n 5 "${tmp_dir}/log.txt"; }
import sys
import matplotlib as mpl
import matplotlib.pyplot as plt
mpl.rcParams['text.usetex'] = True
fig, ax = plt.subplots()
ax.plot([1, 2, 3], [1, 10, 100], label=r'Mnemosyne$^+$ 50\%')
ax.set_yscale('log')
ax.set_xlabel('bits-per-key')
ax.legend()
plt.tight_layout()
fig.savefig(sys.argv[1] + '/test.pdf', bbox_inches="tight")
fig.savefig(sys.argv[1] + '/test.png', bbox_inches="tight")
EOF
    rm -rf "${tmp_dir}"

    # The experiment scripts run "ulimit -n 65535" and abort if that is not allowed
    local hard_limit
    hard_limit=$(ulimit -Hn)
    [ "${hard_limit}" = "unlimited" ] || [ "${hard_limit}" -ge "${MIN_OPEN_FILES}" ] \
        && report 0 "open files hard limit (${hard_limit}) allows 'ulimit -n ${MIN_OPEN_FILES}'" \
        || report 1 "open files hard limit (${hard_limit}) is below ${MIN_OPEN_FILES}, raise it in /etc/security/limits.conf"

    if [ "$all_found" = false ]; then
        echo "👉 Some dependencies are missing. Exiting."
        exit 1
    else
        echo "✅ All dependencies found."
    fi
}

# --- Main Script Logic ---
CHECK_ONLY=false
for arg in "$@"; do
    case "${arg}" in
        --check)
            CHECK_ONLY=true
            ;;
        --texlive-full)
            TEX_PKGS=(texlive-full)
            ;;
        *)
            echo "Error: Invalid argument '${arg}'. Use '--check' or '--texlive-full'."
            exit 1
            ;;
    esac
done

if [ "$CHECK_ONLY" = false ]; then
    install_packages
fi

verify_dependencies

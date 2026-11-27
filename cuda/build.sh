#!/usr/bin/env bash
# Linux build of the GPU Boruvka and of its benchmark. Run from the project root:
#   cuda/build.sh                 builds build/libboruvka_cuda.so and bin/bench_mst
#   ARCH=sm_86 cuda/build.sh      for a specific GPU (default: the one in this machine)
#
# Needs the CUDA toolkit (nvcc) and gcc.
set -euo pipefail

ARCH="${ARCH:-native}"
mkdir -p build bin

# nvcc before 11.6 has no -arch=native: fall back to the compute capability of GPU 0
if [ "$ARCH" = native ]; then
    cap=$(nvidia-smi --query-gpu=compute_cap --format=csv,noheader 2>/dev/null | head -n1 | tr -d '.')
    ARCH="sm_${cap:-50}"
fi

nvcc -O3 -arch="$ARCH" -Xcompiler -fPIC -shared -o build/libboruvka_cuda.so cuda/boruvka.cu

gcc -std=gnu11 -O2 -Iinclude cuda/bench_mst.c src/graph.c src/list.c src/star_impl.c src/matrix_impl.c \
    src/queue.c src/stack.c src/importer.c src/traversal.c src/export.c -lm -ldl -o bin/bench_mst
echo "built build/libboruvka_cuda.so ($ARCH) and bin/bench_mst"

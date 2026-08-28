#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 /path/to/T-MATS /path/to/output.csv" >&2
    exit 2
fi

task_tmats_root=$(realpath "$1")
task_output_csv=$(realpath -m "$2")
task_repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
task_matlab_bin=${THERMOX_MATLAB_BIN:-/home/ubuntu/local/MATLAB/R2023a/bin/matlab}
expected_revision=ad6e4d5d0d76c229db4eb72ca40ef58d5ddc4014
expected_model_sha256=99d95a3f42a12dd42ee5c4353b8d3ae6db2a54f7ac15a106fc72d0eebbb31376
task_model_path="$task_tmats_root/Trunk/TMATS_Examples/Example_GasTurbine_Dyn/GasTurbine_Dyn_Template.mdl"

[[ -x "$task_matlab_bin" ]] || {
    echo "MATLAB executable not found: $task_matlab_bin" >&2
    exit 1
}
[[ -f "$task_model_path" ]] || {
    echo "NASA T-MATS dynamic model not found: $task_model_path" >&2
    exit 1
}

actual_revision=$(git -C "$task_tmats_root" rev-parse HEAD)
[[ "$actual_revision" == "$expected_revision" ]] || {
    echo "T-MATS revision mismatch: expected $expected_revision, got $actual_revision" >&2
    exit 1
}
actual_model_sha256=$(sha256sum "$task_model_path" | cut -d' ' -f1)
[[ "$actual_model_sha256" == "$expected_model_sha256" ]] || {
    echo "T-MATS model checksum mismatch" >&2
    exit 1
}

task_mex_dir="$task_tmats_root/Trunk/TMATS_Library/MEX"
if [[ $(find "$task_mex_dir" -maxdepth 1 -type f -name '*.mexa64' | wc -l) -lt 20 ]]; then
    THERMOX_TMATS_MEX_DIR="$task_mex_dir" "$task_matlab_bin" -batch \
        "cd(getenv('THERMOX_TMATS_MEX_DIR')); make_file_TMATS"
fi

mkdir -p "$(dirname "$task_output_csv")"
export THERMOX_TMATS_ROOT="$task_tmats_root"
export THERMOX_TMATS_OUTPUT="$task_output_csv"
export THERMOX_REPOSITORY_ROOT="$task_repo_root"
"$task_matlab_bin" -batch \
    "addpath(fullfile(getenv('THERMOX_REPOSITORY_ROOT'),'scripts')); export_tmats_gasturbine_dyn_reference(getenv('THERMOX_TMATS_ROOT'),getenv('THERMOX_TMATS_OUTPUT'))"

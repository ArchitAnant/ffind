#!/bin/bash
# setup_testenv.sh
#
# Creates a realistic benchmark test directory (testdir/) for use with
# benchmark.sh. Populates it with a deep directory tree containing thousands
# of files with mixed extensions — enough to make I/O performance meaningful.
#
# Usage: bash setup_testenv.sh [target_dir]
#   target_dir defaults to "testdir" (relative to cwd)
#
# Run this once before running benchmark.sh.

set -e

TARGET="${1:-testdir}"

echo "Setting up benchmark environment in: $TARGET"
echo ""

# ---- Clean up any previous run ----
if [ -d "$TARGET" ]; then
    echo "Removing existing $TARGET ..."
    rm -rf "$TARGET"
fi

mkdir -p "$TARGET"

# ---- Configuration ----
# Total files created will be roughly:
#   NUM_TOP_DIRS * NUM_MID_DIRS * NUM_LEAF_DIRS * FILES_PER_DIR
# Default: 6 * 8 * 5 * 40 = 9,600 files across 240 leaf dirs
NUM_TOP_DIRS=6
NUM_MID_DIRS=8
NUM_LEAF_DIRS=5
FILES_PER_DIR=40

# File extensions to scatter across the tree
# The benchmark searches for *.txt — keep ~10% as .txt
EXTENSIONS=("txt" "txt" "c" "h" "log" "dat" "json" "sh" "md" "py" "o" "out")
NUM_EXTS=${#EXTENSIONS[@]}

total_files=0
total_dirs=0

for t in $(seq 1 $NUM_TOP_DIRS); do
    top_dir="$TARGET/project_$t"
    mkdir -p "$top_dir"

    for m in $(seq 1 $NUM_MID_DIRS); do
        mid_dir="$top_dir/module_$m"
        mkdir -p "$mid_dir"

        # Put some files directly at the mid level
        for f in $(seq 1 10); do
            ext="${EXTENSIONS[$((RANDOM % NUM_EXTS))]}"
            # Give some predictable names too
            echo "mid-level-content-$t-$m-$f" > "$mid_dir/mid_file_${f}.${ext}"
            ((total_files++))
        done

        for l in $(seq 1 $NUM_LEAF_DIRS); do
            leaf_dir="$mid_dir/pkg_$l"
            mkdir -p "$leaf_dir"
            ((total_dirs++))

            for f in $(seq 1 $FILES_PER_DIR); do
                ext="${EXTENSIONS[$((RANDOM % NUM_EXTS))]}"
                filename="file_${t}_${m}_${l}_${f}.${ext}"
                echo "content-$t-$m-$l-$f" > "$leaf_dir/$filename"
                ((total_files++))
            done

            # Always create at least 2 guaranteed .txt files per leaf dir
            # so -name "*.txt" always has something to find
            echo "guaranteed-txt-1" > "$leaf_dir/notes_${t}_${m}_${l}.txt"
            echo "guaranteed-txt-2" > "$leaf_dir/readme_${t}_${m}_${l}.txt"
            ((total_files += 2))
        done
    done
done

# ---- Create a few deeply nested paths to stress recursion ----
deep="$TARGET/deep"
mkdir -p "$deep"
current="$deep"
for depth in $(seq 1 20); do
    current="$current/level_$depth"
    mkdir -p "$current"
    echo "deep-file-$depth" > "$current/deep_note_$depth.txt"
    echo "deep-data-$depth"  > "$current/deep_data_$depth.dat"
    ((total_files += 2))
    ((total_dirs++))
done

# ---- Create a flat dir with thousands of files (stress readdir) ----
flat="$TARGET/flat_stress"
mkdir -p "$flat"
echo "Creating flat stress directory with 2000 files..."
for f in $(seq 1 2000); do
    ext="${EXTENSIONS[$((RANDOM % NUM_EXTS))]}"
    echo "$f" > "$flat/flat_file_$f.$ext"
    ((total_files++))
done
# Sprinkle in guaranteed .txt files
for f in $(seq 1 100); do
    echo "stress-txt-$f" > "$flat/stress_note_$f.txt"
    ((total_files++))
done

echo ""
echo "Done."
echo "  Target dir : $TARGET"
echo "  Total dirs : $total_dirs"
echo "  Total files: $total_files (approx)"
echo ""
echo "You can now run:  bash benchmark.sh"

#!/bin/bash
# Living Worlds Benchmark Script
# Runs multiple configurations and records each session

set -e

# Configuration arrays
GRIDS=(512 1024 2048 3072)
SPEEDS=(1.0 10.0 100.0 500.0 1000.0)
DURATION=24

# Get absolute path of project root
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN_DIR="$PROJECT_ROOT/build/bin"

# Output directories (absolute paths)
RESULTS_DIR="$PROJECT_ROOT/benchmark_results"
RECORDINGS_DIR="$PROJECT_ROOT/benchmark_recordings"
mkdir -p "$RESULTS_DIR" "$RECORDINGS_DIR"

# Get actual screen size
SCREEN_RES=$(xdpyinfo | awk '/dimensions/{print $2}' | head -1)
SCREEN_WIDTH=$(echo $SCREEN_RES | cut -d'x' -f1)
SCREEN_HEIGHT=$(echo $SCREEN_RES | cut -d'x' -f2)

echo "========================================"
echo "Living Worlds Comprehensive Benchmark"
echo "========================================"
echo "Grid sizes: ${GRIDS[*]}"
echo "Sim speeds: ${SPEEDS[*]}"
echo "Duration per test: ${DURATION}s"
echo "Screen size: ${SCREEN_WIDTH}x${SCREEN_HEIGHT}"
echo "Project root: $PROJECT_ROOT"
echo "========================================"
echo ""

cd "$BIN_DIR"

TOTAL_TESTS=$((${#GRIDS[@]} * ${#SPEEDS[@]}))
CURRENT_TEST=0

for grid in "${GRIDS[@]}"; do
    for speed in "${SPEEDS[@]}"; do
        CURRENT_TEST=$((CURRENT_TEST + 1))
        speed_int=$(echo "$speed * 10" | bc | cut -d. -f1)
        OUTPUT_NAME="grid${grid}_speed${speed_int}"
        RECORDING_FILE="$RECORDINGS_DIR/${OUTPUT_NAME}.mp4"
        CSV_FILE="$RESULTS_DIR/${OUTPUT_NAME}.csv"
        
        echo ""
        echo "[$CURRENT_TEST/$TOTAL_TESTS] Testing: Grid=${grid} Speed=${speed}x"
        echo "----------------------------------------"
        
        # Start app in background
        ./LivingWorlds --benchmark --grid "$grid" --duration "$DURATION" --speed "$speed" &
        APP_PID=$!
        
        # Wait for window to appear
        sleep 3
        
        # Record the full screen from position 0,0 (avoids boundary issues)
        RECORD_DURATION=$((DURATION - 4))
        echo "Recording for ${RECORD_DURATION}s..."
        
        ffmpeg -y -f x11grab -framerate 60 \
               -video_size "${SCREEN_WIDTH}x${SCREEN_HEIGHT}" \
               -i ":0.0+0,0" \
               -t "$RECORD_DURATION" \
               -c:v libx264 -preset ultrafast -crf 28 \
               "$RECORDING_FILE" 2>/dev/null &
        FFMPEG_PID=$!
        
        # Wait for app to finish
        wait $APP_PID 2>/dev/null || true
        
        # Wait for ffmpeg to finish
        wait $FFMPEG_PID 2>/dev/null || true
        
        if [ -f "$RECORDING_FILE" ]; then
            SIZE=$(ls -lh "$RECORDING_FILE" | awk '{print $5}')
            echo "Recording saved: $RECORDING_FILE ($SIZE)"
        else
            echo "Warning: Recording not created"
        fi
        
        # Move CSV to results directory
        mv "benchmark_${grid}_${speed_int}.csv" "$CSV_FILE" 2>/dev/null || true
        
        echo "Completed: $OUTPUT_NAME"
        echo ""
    done
done

echo "========================================"
echo "All benchmarks complete!"
echo "========================================"
echo ""
echo "Results in: $RESULTS_DIR/"
echo "Recordings in: $RECORDINGS_DIR/"
echo ""

# Check recordings
echo "Recordings created:"
ls -lh "$RECORDINGS_DIR"/*.mp4 2>/dev/null || echo "No recordings found"
echo ""

# Aggregate all CSVs
echo "Aggregating results..."
echo "time,fps,frame_ms,grid_size,sim_speed,erosion,biome_ca,test_name" > "$RESULTS_DIR/combined.csv"
for csv in "$RESULTS_DIR"/*.csv; do
    if [[ "$csv" != *"combined.csv" ]]; then
        testname=$(basename "$csv" .csv)
        tail -n +2 "$csv" | while read line; do
            echo "$line,$testname" >> "$RESULTS_DIR/combined.csv"
        done
    fi
done

echo "Combined results saved to: $RESULTS_DIR/combined.csv"
echo ""

# Generate summary
echo "=== SUMMARY ===" | tee "$RESULTS_DIR/summary.txt"
for csv in "$RESULTS_DIR"/*.csv; do
    if [[ "$csv" != *"combined.csv" && "$csv" != *"summary.txt" ]]; then
        testname=$(basename "$csv" .csv)
        avg_fps=$(tail -n +2 "$csv" | awk -F',' '{sum+=$2; count++} END {if(count>0) printf "%.1f", sum/count; else print "N/A"}')
        echo "$testname: Avg FPS = $avg_fps" | tee -a "$RESULTS_DIR/summary.txt"
    fi
done

echo ""
echo "Done!"

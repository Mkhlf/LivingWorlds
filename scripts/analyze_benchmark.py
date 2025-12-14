#!/usr/bin/env python3
"""
Living Worlds Benchmark Analysis Script
- Converts MP4 recordings to GIFs
- Generates performance plots from CSV results
"""

import os
import sys
import subprocess
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path

# Directories
SCRIPT_DIR = Path(__file__).parent
PROJECT_ROOT = SCRIPT_DIR.parent
RESULTS_DIR = PROJECT_ROOT / "benchmark_results"
RECORDINGS_DIR = PROJECT_ROOT / "benchmark_recordings"
GIFS_DIR = PROJECT_ROOT / "benchmark_gifs"
PLOTS_DIR = PROJECT_ROOT / "benchmark_plots"

def convert_videos_to_gifs():
    """Convert all MP4 videos to GIFs using ffmpeg."""
    GIFS_DIR.mkdir(exist_ok=True)
    
    mp4_files = list(RECORDINGS_DIR.glob("*.mp4"))
    if not mp4_files:
        print("No MP4 files found in", RECORDINGS_DIR)
        return
    
    print(f"Converting {len(mp4_files)} videos to GIFs...")
    
    for mp4_file in mp4_files:
        gif_file = GIFS_DIR / f"{mp4_file.stem}.gif"
        print(f"  {mp4_file.name} -> {gif_file.name}")
        
        # Use ffmpeg with palette for better quality
        # Scale to 480px width, 10fps for smaller file size
        cmd = [
            "ffmpeg", "-y", "-i", str(mp4_file),
            "-vf", "fps=10,scale=480:-1:flags=lanczos,split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse",
            "-loop", "0",
            str(gif_file)
        ]
        
        try:
            subprocess.run(cmd, capture_output=True, check=True)
            size_mb = gif_file.stat().st_size / (1024 * 1024)
            print(f"    Created: {size_mb:.1f} MB")
        except subprocess.CalledProcessError as e:
            print(f"    ERROR: {e.stderr.decode()[:100]}")
    
    print(f"\nGIFs saved to: {GIFS_DIR}")

def load_benchmark_data():
    """Load and parse benchmark CSV files."""
    csv_files = list(RESULTS_DIR.glob("grid*.csv"))
    if not csv_files:
        print("No benchmark CSV files found in", RESULTS_DIR)
        return None
    
    all_data = []
    for csv_file in csv_files:
        try:
            df = pd.read_csv(csv_file)
            # Extract grid and speed from filename
            name = csv_file.stem  # e.g., "grid512_speed10"
            parts = name.split("_")
            grid = int(parts[0].replace("grid", ""))
            speed = int(parts[1].replace("speed", "")) / 10.0
            df["grid_size"] = grid
            df["sim_speed"] = speed
            all_data.append(df)
        except Exception as e:
            print(f"Error loading {csv_file}: {e}")
    
    if not all_data:
        return None
    
    return pd.concat(all_data, ignore_index=True)

def plot_fps_by_grid_size(df):
    """Plot average FPS vs grid size."""
    PLOTS_DIR.mkdir(exist_ok=True)
    
    # Group by grid size and calculate mean FPS
    grouped = df.groupby("grid_size")["fps"].mean().reset_index()
    
    fig, ax = plt.subplots(figsize=(10, 6))
    
    bars = ax.bar(grouped["grid_size"].astype(str), grouped["fps"], 
                  color=['#2ecc71', '#3498db', '#9b59b6', '#e74c3c'])
    
    ax.set_xlabel("Grid Size", fontsize=12)
    ax.set_ylabel("Average FPS", fontsize=12)
    ax.set_title("Performance vs Grid Size", fontsize=14, fontweight='bold')
    
    # Add value labels on bars
    for bar, fps in zip(bars, grouped["fps"]):
        ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 20,
                f'{fps:.0f}', ha='center', va='bottom', fontsize=10)
    
    ax.set_ylim(0, grouped["fps"].max() * 1.15)
    ax.grid(axis='y', alpha=0.3)
    
    plt.tight_layout()
    plt.savefig(PLOTS_DIR / "fps_by_grid_size.png", dpi=150)
    print(f"Saved: {PLOTS_DIR / 'fps_by_grid_size.png'}")
    plt.close()

def plot_fps_by_speed(df):
    """Plot FPS vs simulation speed for each grid size."""
    PLOTS_DIR.mkdir(exist_ok=True)
    
    fig, ax = plt.subplots(figsize=(12, 6))
    
    colors = ['#2ecc71', '#3498db', '#9b59b6', '#e74c3c']
    grid_sizes = sorted(df["grid_size"].unique())
    
    for i, grid in enumerate(grid_sizes):
        grid_data = df[df["grid_size"] == grid]
        grouped = grid_data.groupby("sim_speed")["fps"].mean().reset_index()
        ax.plot(grouped["sim_speed"], grouped["fps"], 
                marker='o', linewidth=2, markersize=8,
                color=colors[i % len(colors)], label=f'{grid}×{grid}')
    
    ax.set_xlabel("Simulation Speed Multiplier", fontsize=12)
    ax.set_ylabel("Average FPS", fontsize=12)
    ax.set_title("Performance vs Simulation Speed", fontsize=14, fontweight='bold')
    ax.legend(title="Grid Size", loc='upper right')
    ax.set_xscale('log')
    ax.grid(True, alpha=0.3)
    
    plt.tight_layout()
    plt.savefig(PLOTS_DIR / "fps_by_speed.png", dpi=150)
    print(f"Saved: {PLOTS_DIR / 'fps_by_speed.png'}")
    plt.close()

def generate_summary_table(df):
    """Generate a summary table."""
    PLOTS_DIR.mkdir(exist_ok=True)
    
    summary = df.groupby(["grid_size", "sim_speed"]).agg({
        "fps": ["mean", "min", "max", "std"]
    }).round(1)
    
    summary.columns = ["Avg FPS", "Min FPS", "Max FPS", "Std Dev"]
    summary = summary.reset_index()
    
    # Save as CSV
    summary.to_csv(PLOTS_DIR / "summary_table.csv", index=False)
    print(f"Saved: {PLOTS_DIR / 'summary_table.csv'}")
    
    # Print table
    print("\n=== Performance Summary ===")
    print(summary.to_string(index=False))

def main():
    print("=" * 50)
    print("Living Worlds Benchmark Analysis")
    print("=" * 50)
    print()
    
    # 1. Convert videos to GIFs
    if RECORDINGS_DIR.exists():
        convert_videos_to_gifs()
    else:
        print(f"Recordings directory not found: {RECORDINGS_DIR}")
    
    print()
    
    # 2. Generate plots
    df = load_benchmark_data()
    if df is not None:
        print(f"Loaded {len(df)} data points from benchmark results")
        print()
        
        plot_fps_by_grid_size(df)
        plot_fps_by_speed(df)
        generate_summary_table(df)
        
        print(f"\nAll plots saved to: {PLOTS_DIR}")
    else:
        print("No benchmark data to plot")
    
    print("\nDone!")

if __name__ == "__main__":
    main()

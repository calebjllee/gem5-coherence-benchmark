#!/usr/bin/env python3
import os
import re
import csv
from pathlib import Path
from collections import defaultdict

# Configuration
RESULTS_DIR = os.path.join(os.getcwd(), "B_hot_rpw_sweep_4c")
OUTPUT_CSV = os.path.join(os.getcwd(), "stats_sweep4_byte.csv")

# Mode mapping
MODE_MAP = {
    "hot": "Max Sharing",
    "false": "False Sharing",
    "padded": "No Sharing",
}

def parse_folder_name(folder_name):
    """Extract Protocol, Cores, Mode, and RPW from folder name."""
    parts = folder_name.split("_")
    
    protocol = parts[0] if len(parts) > 0 else ""
    
    # Extract cores (e.g., "8c" -> 8)
    cores = ""
    for part in parts:
        if part.endswith("c") and part[:-1].isdigit():
            cores = part[:-1]
            break
    
    # Extract mode by matching known labels (false, padded, hot)
    mode_str = ""
    mode_display = ""
    for part in parts:
        if part in MODE_MAP:
            mode_str = part
            mode_display = MODE_MAP.get(mode_str, mode_str)
            break
    # Extract RPW value from tokens like rpwX
    rpw = ""
    for part in parts:
        m = re.fullmatch(r"rpw(\d+)", part)
        if m:
            rpw = m.group(1)
            break

    return protocol, cores, mode_display, rpw

def parse_stats_file(file_path):
    """Parse stats.txt file and extract metrics."""
    stats = {}
    
    try:
        with open(file_path, 'r') as f:
            content = f.read()
            
            # Extract simTicks
            match = re.search(r'simTicks\s+(\d+)', content)
            if match:
                stats['simTicks'] = match.group(1)
            
            # Extract L1 Cache Hits
            match = re.search(r'l1_cntrl0\.cacheMemory\.m_demand_hits\s+(\d+)', content)
            if match:
                stats['L1 Cache Hits'] = match.group(1)
            
            # Extract L1 Cache Misses
            match = re.search(r'l1_cntrl0\.cacheMemory\.m_demand_misses\s+(\d+)', content)
            if match:
                stats['L1 Cache Misses'] = match.group(1)
            
            # Extract all system.ruby.network.msg_count.NAME entries
            pattern = r'system\.ruby\.network\.msg_byte\.(\S+)\s+(\d+)'
            for match in re.finditer(pattern, content):
                msg_name = match.group(1)
                msg_count = match.group(2)
                stats[msg_name] = msg_count
    
    except Exception as e:
        print(f"Error reading {file_path}: {e}")
    
    return stats

def main():
    # Check if results directory exists
    if not os.path.isdir(RESULTS_DIR):
        print(f"Error: {RESULTS_DIR} not found")
        return
    
    # Collect all data and column names
    rows = []
    all_msg_names = set()
    
    # Iterate through folders in results directory
    for folder_name in sorted(os.listdir(RESULTS_DIR)):
        folder_path = os.path.join(RESULTS_DIR, folder_name)
        
        # Skip if not a directory
        if not os.path.isdir(folder_path):
            continue
        
        stats_file = os.path.join(folder_path, "stats.txt")
        
        # Skip if stats.txt doesn't exist
        if not os.path.isfile(stats_file):
            print(f"Warning: {stats_file} not found, skipping {folder_name}")
            continue
        
        # Parse folder name
        protocol, cores, mode, rpw = parse_folder_name(folder_name)
        
        # Parse stats file
        stats = parse_stats_file(stats_file)
        
        # Collect message names for later
        for key in stats.keys():
            if key not in ['simTicks', 'L1 Cache Hits', 'L1 Cache Misses']:
                all_msg_names.add(key)
        
        # Build row
        row = {
            'Protocol': protocol,
            'Reads Per Write': mode,
            'RPW': rpw,
            'Cores': cores,
            'simTicks': stats.get('simTicks', ''),
            'L1 Cache Hits': stats.get('L1 Cache Hits', ''),
            'L1 Cache Misses': stats.get('L1 Cache Misses', ''),
        }
        
        # Add message counts
        for msg_name in sorted(all_msg_names):
            row[msg_name] = stats.get(msg_name, '')
        
        rows.append(row)
    
    # Define final column order
    base_columns = ['Protocol', 'Reads Per Write', 'RPW', 'Cores', 'simTicks', 'L1 Cache Hits', 'L1 Cache Misses']
    msg_columns = sorted(list(all_msg_names))
    fieldnames = base_columns + msg_columns
    
    # Write CSV
    try:
        with open(OUTPUT_CSV, 'w', newline='') as csvfile:
            writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
            writer.writeheader()
            
            # Re-populate rows with all columns to ensure consistent structure
            for row in rows:
                for col in fieldnames:
                    if col not in row:
                        row[col] = ''
            
            writer.writerows(rows)
        
        print(f"✅ CSV generated successfully: {OUTPUT_CSV}")
        print(f"   {len(rows)} rows, {len(fieldnames)} columns")
    
    except Exception as e:
        print(f"Error writing CSV: {e}")

if __name__ == "__main__":
    main()

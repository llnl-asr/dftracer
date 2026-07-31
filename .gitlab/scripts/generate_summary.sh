#!/bin/bash

log() {
    echo "$(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_FILE"
}


log "Starting generate_summary.sh on $(hostname)"

log "Exporting DFTRACER_VERSION..."
if [ "x$DFTRACER_VERSION" == "x" ]; then
    export DFTRACER_VERSION=$(python -c "import dftracer; print(dftracer.__version__)") || { echo "Failed to get DFTRACER_VERSION"; exit 1; }
fi
log "DFTRACER_VERSION: $DFTRACER_VERSION"

LFS_DIR=v$DFTRACER_VERSION/$SYSTEM_NAME
ROOT_PATH=$LOG_STORE_DIR/$LFS_DIR
CSV_FILE=$ROOT_PATH/trace_paths.csv
COMPARE_CSV_FILE=$ROOT_PATH/compare.csv

log "Setting up CSV file at $CSV_FILE..."
echo "workload_name,num_nodes,ci_date,trace_path,trace_size_bytes,trace_size_fmt,num_events" > "$CSV_FILE"

log "Starting traversal of workload directories in $ROOT_PATH..."
for workload_path in "$ROOT_PATH"/*; do
    if [ ! -d "$workload_path" ]; then
        log "Skipping non-directory: $workload_path"
        continue
    fi
    workload_name=$(basename "$workload_path")
    log "Processing workload: $workload_name"
    
    for node_path in "$workload_path"/nodes-*; do
        if [ -d "$node_path" ]; then
            node_config=$(basename "$node_path")
            node_num=${node_config#nodes-}
            log "Processing node configuration: $node_config (Nodes: $node_num)"
            
            latest_timestamp=$(find "$node_path" -mindepth 1 -maxdepth 1 -type d -exec basename {} \; | sort -r | head -n 1)
            
            if [ -n "$latest_timestamp" ]; then
                log "Latest timestamp found: $latest_timestamp"

                full_path="$node_path/$latest_timestamp/RAW"
                if [ ! -d "$full_path" ]; then
                    full_path="$node_path/$latest_timestamp/COMPACT"
                fi
                if [ -d "$full_path" ]; then
                    log "Found trace path: $full_path"
                    shopt -s nullglob
                    zindex_files=("$full_path"/*.zindex)
                    if [ ${#zindex_files[@]} -eq 0 ]; then
                        log "No index found in: $full_path"
                        dftracer_create_index -f -d "$full_path/"
                    else 
                        log "Index found in: $full_path"
                    fi
                    size_bytes=$(du -b "$full_path" | cut -f1)
                    size_formatted=$(du -sh "$full_path" | cut -f1)
                    event_counts=$(dftracer_event_count -d "$full_path")
                    
                    log "Size: $size_formatted"
                    relative_path=$(realpath --relative-to="$ROOT_PATH" "$full_path")
                    log "Relative path: $relative_path"
                    echo "$workload_name,$node_num,$latest_timestamp,$relative_path,$size_bytes,$size_formatted,$event_counts" >> "$CSV_FILE"
                else
                    log "No COMPACT directory found at $full_path"
                fi
            else
                log "No timestamp directories found in $node_path"
            fi
        else
            log "Skipping non-directory: $node_path"
        fi
    done
done

num_lines=$(wc -l < "$CSV_FILE")
log "CSV file created at: $CSV_FILE with $num_lines lines"

log "Sorting CSV file by workload_name and num_nodes..."
header=$(head -n 1 "$CSV_FILE")
tail -n +2 "$CSV_FILE" | sort -t, -k1,1 -k2,2n > "${CSV_FILE}.sorted"
echo "$header" > "$CSV_FILE"
cat "${CSV_FILE}.sorted" >> "$CSV_FILE"
rm "${CSV_FILE}.sorted"
log "CSV file sorted successfully."

if [[ $num_lines -eq 1 ]]; then
    log "No trace paths found. Cleaning up..."
    rm -rf $ROOT_PATH
    exit 1
else 
    log "Preparing to commit and push files..."
    cd $ROOT_PATH
    git add .gitattributes .gitignore prepare_traces.sh README.md trace_paths.csv 
    git commit -m "added initial files" || { log "Failed to commit files"; exit 1; }
    git push origin "$LFS_DIR" || { log "Failed to push branch $LFS_DIR"; exit 1; }
    log "Files committed and pushed successfully."
    cd - || { log "Failed to return to previous directory"; exit 1; }
fi

log "generate_summary.sh completed."

BASELINE=/p/lustre3/iopp/dftracer-traces-lfs/v1.0.10.dev6/corona/trace_paths.csv
log "Starting comparison of summary files..."
python .gitlab/scripts/compare_summary.py ${BASELINE} "$CSV_FILE" --output_file "$COMPARE_CSV_FILE"
log "Comparison completed. Output written to $COMPARE_CSV_FILE"

log "Changing permissions for $ROOT_PATH..."
"$LOG_STORE_DIR/chgperm.sh" "$ROOT_PATH"
log "Permission change completed."

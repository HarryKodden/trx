#!/bin/bash
# Wrapper script for load_test.py - handles virtual environment automatically

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="$SCRIPT_DIR/venv"
PYTHON_SCRIPT="$SCRIPT_DIR/load_test.py"

# Check if virtual environment exists
if [ ! -d "$VENV_DIR" ]; then
    echo "Setting up virtual environment..."
    python3 -m venv "$VENV_DIR"
    source "$VENV_DIR/bin/activate"
    pip install requests
else
    source "$VENV_DIR/bin/activate"
fi

# Run the load test script with all arguments
python "$PYTHON_SCRIPT" "$@"

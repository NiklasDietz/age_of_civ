#!/bin/bash
# Launch Age of Civilization from the build directory
cd "$(dirname "$0")/build/debug" && exec ./age_of_civ "$@"

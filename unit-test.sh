#!/bin/bash
# Run unit tests for the assignment

# Automate these steps from the readme:
# Create a build subdirectory, change into it, run
# cmake .. && make && run the assignment-autotest application
set -e
set -x
mkdir -p build
cd build
cmake ..
make clean
make
cd ..
pwd
ls -la build/assignment-autotest
./build/assignment-autotest/assignment-autotest
rc=$?
echo "Exit code of assignment-autotest is $rc"
exit $rc


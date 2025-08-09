#!/bin/zsh
set -euxo pipefail

nvcc -Xcompiler -fPIC -shared -I/usr/include/python3.10 -o _pdl_detector.so pdl_detector.cpp -lcupti

#!/bin/bash

# use '$1' for the path to the root of the ported source code
buildcmd1 $1/tests/<vendor-name>/<board-name>/<build-config-path> <other-build-args>
buildcmd2 $1/tests/<vendor-name>/<board-name>/<build-config-path> <other-build-args>
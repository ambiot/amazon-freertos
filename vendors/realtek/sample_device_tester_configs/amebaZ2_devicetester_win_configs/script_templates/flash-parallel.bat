#!/bin/bash

# use '$1' for the path to the root of the ported source code
# use '$2' for actual serial port number if serial port is used
flashcmd1 $1/<relative-path-to>/<flash-config-path> -serial-port $2 <other-flash-args>
flashcmd2 $1/<relative-path-to>/<flash-config-path> <other-flash-args>

#!/bin/sh

# This script allows te to create a log of the interactive session.
# te's "-l" option causes it to output on stderr, as well as the tty.
# te.sh then redirects stderr to te.sh's argument.
if [ -n "$1" ]; then
    ./te -l 2> "$1"
else
    ./te
fi

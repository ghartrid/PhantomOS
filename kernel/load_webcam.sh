#!/bin/bash
# Load webcam driver for PhantomOS face tracking
# This script needs to be run with sudo or via pkexec

if [ ! -e /dev/video0 ]; then
    echo "Loading uvcvideo kernel module..."
    modprobe uvcvideo
    sleep 1

    if [ -e /dev/video0 ]; then
        echo "Webcam ready at /dev/video0"
    else
        echo "Warning: Module loaded but no video device appeared"
        echo "Check USB connection to webcam"
    fi
else
    echo "Webcam already available at /dev/video0"
fi

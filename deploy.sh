#!/usr/bin/env sh
#A quick/dirty script to deploy to a rpi named 'rpi'.. this should be replaced by apt or something
HOST=rpi
rsync -avz --exclude build/ --delete ../RNBOJackRunner ${HOST}:local/src/
rsync -avz --exclude build/ --delete ../../src/cpp/ ${HOST}:local/src/rnbo

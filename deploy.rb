#!/usr/bin/env ruby
#A quick/dirty script to deploy to a rpi.. this should be replaced by apt or something
remote = ARGV[0]
raise "you must provide a remote, eg pi@c74rpi.local name as the first argument" unless remote
system("rsync -avz --exclude build/ --exclude RNBOOSCQueryRunner/docker/ --exclude deploy.rb --delete ../RNBOOSCQueryRunner #{remote}:local/src/")
system("rsync -avz --exclude build/ --delete ../../src/cpp/ #{remote}:local/src/rnbo")

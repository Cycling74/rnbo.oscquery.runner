#!/usr/bin/env ruby
#A quick/dirty script to deploy to an rpi for dev work
remote = ARGV[0]
raise "you must provide a remote, eg pi@c74rpi.local name as the first argument" unless remote
system("rsync -avz --exclude 'build*/' --exclude rnbo.oscquery.runner/docker/ --exclude deploy.rb --exclude '.ccls-cache/' --delete ../rnbo.oscquery.runner #{remote}:local/src/")
system("rsync -avz --exclude 'build*/' --delete ../../src/cpp/ #{remote}:local/src/rnbo")

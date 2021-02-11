require 'fileutils'

VERSION = "0.1.0".freeze
SCRIPT = "rnbo-update-service".freeze
DIR = File.join("build", "rnbo-update-service-#{VERSION}")

FileUtils.mkdir_p(DIR)
FileUtils.cp(SCRIPT, DIR)

Dir.chdir(DIR) do
	raise "failed to dh_make" unless system("dh_make --indep --createorig")
	File.open("debian/install", "w") do |f|
		f.print("#{SCRIPT} usr/bin")
	end
	raise "failed to build" unless system("debuild -us -uc")
end

require 'fileutils'

#requires apt-get install dh-make devscripts

VERSION = "0.1.0".freeze
SCRIPT = "rnbo-update-service".freeze
SERVICE_FILE = "rnbo-update-service.service".freeze
DBUS_FILE = "rnbo-system.conf".freeze

DIR = File.join("build", "rnbo-update-service-#{VERSION}")

#set env vars for debian
ENV["DEBEMAIL"] = "xnor@cylcing74.com"
ENV["DEBFULLNAME"] = "Alex Norman"

FileUtils.mkdir_p(DIR)
FileUtils.mkdir_p(DIR)
FileUtils.cp(SCRIPT, DIR)
FileUtils.cp(DBUS_FILE, DIR)

Dir.chdir(DIR) do
  File.open("Makefile", "w") do |f|
    f.print <<EOF
all:

install:
	install -D #{SCRIPT} $(DESTDIR)/usr/bin/#{SCRIPT}

uninstall:
	-rm -f $(DESTDIR)/usr/bin/#{SCRIPT}

clean:

distclean:

.PHONY: all install uninstall clean distclean
EOF
  end

  raise "failed to dh_make" unless system("dh_make --indep --createorig -c mit --yes")

begin
  File.open("debian/rnbo-update-service.install", "w") do |f|
    f.puts("#{DBUS_FILE} etc/dbus-1/system.d")
  end
end

  File.open("debian/rules", "w") do |f|
    f.print <<EOF
#!/usr/bin/make -f
export DESTROOT=$(CURDIR)/debian/rnbo-update-service
%:
	dh $@
EOF
  end

  control = "debian/control"
  content = File.readlines(control).collect do |l|
    if l =~ /\AHomepage/
      "Homepage: http://rnbo.cycling74.com"
    elsif l =~ /\ADepends/
      "#{l.chomp}, ruby (>= 1:2.5), ruby-dbus (>= 0.15)"
    elsif l =~ /\ASection/
      "Section: admin"
    elsif l =~ /\ADescription/
      "Description: A service for managing the version of the rnbooscquery runner."
    elsif l =~ /insert long description/
      "  This service communicates via dbus to let the rnbooscquery specify versions to update it to."
    else
      l
    end
  end
  File.open(control, "w") do |f|
    content.each { |l| f.puts l }
  end

  changelog = "debian/changelog"
  content = File.readlines(changelog).collect do |l|
    if l =~ /\* Initial release/
      "  * Initial release"
    else
      l
    end
  end
  File.open(changelog, "w") do |f|
    content.each { |l| f.puts l }
  end

  copyright = "debian/copyright"
  content = File.readlines(copyright).collect do |l|
    if l =~ /\ASource/
      "Source: http://rnbo.cycling74.com"
    elsif l =~ /\ACopyright/
      "Copyright: 2021 Alex Norman <xnor@cycling74.com>"
    elsif l =~ /\A\s*<years>/
      "#"
    else
      l
    end
  end.reject { |l| l =~ /\A\s*#/ }
  File.open(copyright, "w") do |f|
    content.each { |l| f.puts l }
  end

  #remove files we don't need
  FileUtils.rm(["README.source", "README.Debian", "rnbo-update-service-docs.docs", "rnbo-update-service.doc-base.EX"].collect { |f| File.join("debian", f) } )
  Dir.glob("debian/*.ex") do |f|
    FileUtils.rm(f)
  end

  #setup systemd
  FileUtils.cp("../../#{SERVICE_FILE}", "debian/")
  raise "failed to enable systemd" unless system("dh_installsystemd --restart-after-upgrade")
  raise "failed to dh_install" unless system("dh_install")

  raise "failed to build" unless system("debuild -us -uc")
end

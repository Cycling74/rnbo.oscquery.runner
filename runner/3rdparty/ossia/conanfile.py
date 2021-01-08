from conans import ConanFile, CMake, tools


class LibossiaConan(ConanFile):
    name = "libossia"
    version = "1.2.1"
    license = "Available under both LGPLv3 and CeCILL-C"
    #author = "<Put your name here> <And your email here>"
    url = "https://opencollective.com/ossia"
    description = "A modern C++, cross-environment distributed object model for creative coding."
    topics = ("creative-coding", "osc", "open-sound-control", "ossia", "oscquery")
    settings = "os", "compiler", "build_type", "arch"
    options = {}
    default_options = {}
    generators = "cmake"

    def source(self):
        self.run("git clone --branch master https://github.com/ossia/libossia.git")

    def configure_cmake(self):
        cmake = CMake(self)

        cmake.definitions["OSSIA_CPP"] = True
        cmake.definitions["OSSIA_CPP_ONLY"] = True
        cmake.definitions["OSSIA_DATAFLOW"] = False
        cmake.definitions["OSSIA_EDITOR"] = False
        cmake.definitions["OSSIA_GFX"] = False
        cmake.definitions["OSSIA_PCH"] = False
        cmake.definitions["OSSIA_PROTOCOL_ARTNET"] = False
        cmake.definitions["OSSIA_PROTOCOL_AUDIO"] = False
        cmake.definitions["OSSIA_PROTOCOL_MIDI"] = False
        cmake.definitions["OSSIA_PROTOCOL_WIIMOTE"] = False
        cmake.definitions["OSSIA_PROTOCOL_JOYSTICK"] = False

        return cmake

    def build(self):
        cmake = self.configure_cmake()
        cmake.configure(source_folder="libossia")
        cmake.build()

    def package(self):
        cmake = self.configure_cmake()
        cmake.install()
        #self.copy("*.h", dst="include", src="hello"
        #self.copy("*ossia.lib", dst="lib", keep_path=False)
        #self.copy("*.dll", dst="bin", keep_path=False)
        #self.copy("*.so", dst="lib", keep_path=False)
        #self.copy("*.dylib", dst="lib", keep_path=False)
        #self.copy("*.a", dst="lib", keep_path=False)

    def package_info(self):
        self.cpp_info.libs = ["ossia"]
        self.cpp_info.includedirs = ["include"]

from conans import ConanFile, CMake, tools


class LibossiaConan(ConanFile):
    name = "libossia"
    version = "1.2.1"
    license = "Available under both LGPLv3 and CeCILL-C"
    url = "https://opencollective.com/ossia"
    description = "A modern C++, cross-environment distributed object model for creative coding."
    topics = ("creative-coding", "osc", "open-sound-control", "ossia", "oscquery")
    settings = "os", "compiler", "build_type", "arch"
    options = {"fPIC": [True, False], "shared": [True, False]}
    default_options = {"fPIC": True, "shared": False}
    exports_sources = ["CMakeLists.txt"]
    generators = "cmake"

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def configure(self):
        if self.options.shared:
            del self.options.fPIC

    def source(self):
        self.run("git clone --branch master https://github.com/ossia/libossia.git")

    def configure_cmake(self):
        cmake = CMake(self)

        cmake.definitions["OSSIA_DATAFLOW"] = False
        cmake.definitions["OSSIA_EDITOR"] = False
        cmake.definitions["OSSIA_GFX"] = False
        cmake.definitions["OSSIA_JAVA"] = False
        cmake.definitions["OSSIA_PD"] = False
        cmake.definitions["OSSIA_MAX"] = False
        cmake.definitions["OSSIA_PYTHON"] = False
        cmake.definitions["OSSIA_QT"] = False
        cmake.definitions["OSSIA_C"] = False
        cmake.definitions["OSSIA_CPP"] = True
        cmake.definitions["OSSIA_UNITY3D"] = False
        cmake.definitions["OSSIA_QML"] = False
        cmake.definitions["OSSIA_QML_SCORE"] = False
        cmake.definitions["OSSIA_QML_DEVICE"] = False
        cmake.definitions["OSSIA_DISABLE_QT_PLUGIN"] = True
        cmake.definitions["OSSIA_DNSSD"] = True

        cmake.definitions["OSSIA_PROTOCOL_AUDIO"] = False
        cmake.definitions["OSSIA_PROTOCOL_MIDI"] = False
        cmake.definitions["OSSIA_PROTOCOL_OSC"] = True
        cmake.definitions["OSSIA_PROTOCOL_MINUIT"] = False
        cmake.definitions["OSSIA_PROTOCOL_OSCQUERY"] = True
        cmake.definitions["OSSIA_PROTOCOL_HTTP"] = False
        cmake.definitions["OSSIA_PROTOCOL_WEBSOCKETS"] = False
        cmake.definitions["OSSIA_PROTOCOL_SERIAL"] = False
        cmake.definitions["OSSIA_PROTOCOL_PHIDGETS"] = False
        cmake.definitions["OSSIA_PROTOCOL_LEAPMOTION"] = False
        cmake.definitions["OSSIA_PROTOCOL_JOYSTICK"] = False
        cmake.definitions["OSSIA_PROTOCOL_WIIMOTE"] = False
        cmake.definitions["OSSIA_PROTOCOL_ARTNET"] = False
        cmake.definitions["OSSIA_PROTOCOL_LIBMAPPER"] = False

        return cmake

    def build(self):
        cmake = self.configure_cmake()
        cmake.configure(source_folder="libossia")
        cmake.build()

    def package(self):
        cmake = self.configure_cmake()
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = tools.collect_libs(self)
        self.cpp_info.includedirs = ["include"]

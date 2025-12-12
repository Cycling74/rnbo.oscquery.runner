from conan import ConanFile
from conan.tools.cmake import cmake_layout, CMakeToolchain, CMakeDeps, CMake

class RNBORunnerRecipe(ConanFile):
    settings = "os", "compiler", "build_type", "arch"

    options = {
        "dbus": [True, False],
    }

    default_options = {
        "dbus": False
    }

    def requirements(self):
        self.requires("libossia/v2.0.0-rc6-133-gad48e52a1", options = { "shared": False })
        self.requires("cpp-optparse/cci.20171104")
        self.requires("base64/0.5.2")
        self.requires("libsndfile/1.2.2")
        self.requires("sqlitecpp/3.3.3");
        self.requires("boost/1.83.0")

    def build_requirements(self):
        self.tool_requires("cmake/3.27.9")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.cache_variables["WITH_DBUS"] = bool(self.options.dbus)
        #tc.preprocessor_definitions["MYDEFINE"] = "MYDEF_VALUE"
        tc.generate()
        deps = CMakeDeps(self)
        deps.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

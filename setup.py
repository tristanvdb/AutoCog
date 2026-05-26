#!/usr/bin/env python3

import os
import sys
import shutil
import subprocess
from pathlib import Path
from pybind11.setup_helpers import Pybind11Extension, build_ext
from pybind11 import get_cmake_dir
import pybind11

from setuptools import setup, Extension

class CMakeExtension(Extension):
    def __init__(self, name, sourcedir=""):
        Extension.__init__(self, name, sources=[])
        self.sourcedir = os.path.abspath(sourcedir)

class CMakeBuild(build_ext):
    def build_extension(self, ext):
        extdir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))

        package_dir = Path(extdir).parent

        debug = int(os.environ.get("DEBUG", 0)) if self.debug is None else self.debug
        cfg = "Debug" if debug else "Release"

        cmake_args = [
            f"-DCMAKE_INSTALL_PREFIX={package_dir}",  # Install to package root
            f"-DPYTHON_EXECUTABLE={sys.executable}",
            f"-DCMAKE_BUILD_TYPE={cfg}",
            f"-Dpybind11_DIR={get_cmake_dir()}",
        ]

        build_args = ["--config", cfg]
        
        if "CMAKE_ARGS" in os.environ:
            cmake_args += [item for item in os.environ["CMAKE_ARGS"].split(" ") if item]

        if "CMAKE_BUILD_PARALLEL_LEVEL" not in os.environ:
            if hasattr(self, "parallel") and self.parallel:
                build_args += [f"-j{self.parallel}"]

        build_temp = Path(self.build_temp)
        build_temp.mkdir(parents=True, exist_ok=True)

        subprocess.check_call(
            ["cmake", ext.sourcedir] + cmake_args, cwd=build_temp
        )

        subprocess.check_call(
            ["cmake", "--build", "."] + build_args, cwd=build_temp
        )

        subprocess.check_call(
            ["cmake", "--install", "."], cwd=build_temp
        )

def main():
    ext_modules = [
        CMakeExtension("autocog._build_all", sourcedir="."),
    ]

    setup(
        ext_modules=ext_modules,
        cmdclass={"build_ext": CMakeBuild},
        zip_safe=False,
    )

if __name__ == "__main__":
    main()

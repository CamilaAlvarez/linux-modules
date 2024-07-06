from setuptools import Extension, setup
from Cython.Build import cythonize

setup(
    ext_modules = cythonize([Extension("homedomotics", ["homedomotics-sensors.pyx"])])
)

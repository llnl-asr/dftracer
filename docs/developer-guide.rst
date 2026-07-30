======================
Developer Guide
======================

------------------------------------------
Publishing package
------------------------------------------

For publishing the package we use :code:`setuptools_scm` to manage the versioning of the package based on :code:`git tags`. 

Below are the common commands used for to check the right version is being generated and to create new releases locally.

.. code-block:: bash

    git checkout -b <branch>
    git add <file>
    git commit -m <message> 
    git tag -a <tag> -m <message> # for python use v<MAJOR>.<MINOR>.<PATCH>
    pip install setuptools-scm

    python -c "
    from setuptools_scm import get_version
    from setuptools_scm.version import ScmVersion

    def myversion_func(version: ScmVersion) -> str:
        from setuptools_scm.version import only_version
        print(f'Debug info:')
        print(f'  Tag: {version.tag}')
        print(f'  Distance: {version.distance}')
        print(f'  Dirty: {version.dirty}')
        print(f'  Node: {version.node}')
        if version.distance and version.distance > 0:
            result = version.format_next_version(only_version, fmt='{tag}.post{distance}')
            print(f'  Result (distance > 0): {result}')
            return result
        else:
            result = version.format_next_version(only_version, fmt='{tag}')
            print(f'  Result (distance == 0): {result}')
            return result

    version = get_version(version_scheme=myversion_func)
    print(f'\nFinal version: {version}')
    "

------------------------------------------
ALCF Polaris
------------------------------------------

These are steps that are needed to compile :code:`dftracer` on `ALCF Polaris <https://docs.alcf.anl.gov/polaris/getting-started/>`_

First, make sure you have set up the environment variable and source it as shown `here <https://docs.alcf.anl.gov/polaris/data-science-workflows/python/>`_. Then, you can modify the :code:`dftracer` codebase and compile the codebase by running commands below:

.. code-block:: bash

   module use /soft/modulefiles
   module load conda
   module unload darshan
   conda activate base
   source <your venv>

   export CC=cc
   export CXX=CC
   export CMAKE_BUILD_TYPE=PROFILE
   export DFTRACER_ENABLE_TESTS=On
   export DFTRACER_LOGGER_USER=1
   export DFTRACER_DISABLE_HWLOC=On
   export DFTRACER_TEST_LD_LIBRARY_PATH=/opt/cray/libfabric/1.15.2.0/lib64
   pip install -v ".[dfanalyzer]"

.. note::

   We need to disable :code:`darshan` here because it will give you a lot of :code:`segfault` on Polaris machine due to POSIX API interceptor done by Darshan

Then, to run the the test, you need to run commands below:

.. code-block:: bash

   module use /soft/modulefiles
   module load conda
   module unload darshan
   conda activate base
   source <your venv>

   pip install -r test/py/requirements.txt
   pushd build/temp*/*dftracer*/
   ctest -E dlio -VV --debug --stop-on-failure
   popd


Updating Docs
=============

For updating the docs we need to install additional dependency :code:`Sphinx`

.. code-block:: bash

   module use /soft/modulefiles
   module load conda
   module unload darshan
   conda activate base
   source <your venv>

   pip install "Sphinx<7"

   cd <dftracer>/docs
   make html
   
                
Then open :code:`_build/html/index.html`




Managing Configuration
==============

Importing config variables in your code is done by including the header file:

.. code-block:: cpp
   #ifdef DFTRACER_DEBUG
   #include <dftracer/dftracer_config_dbg.hpp>
   #else
   #include <dftracer/dftracer_config.hpp>
   #endif
   #ifdef DFTRACER_MY_OPTION_ENABLE 
   // Define MY_OPTION dependent code here
   #endif

Enabling configuration options is done in the :code:`cmake/dftracer_config.hpp` and
the :code:`cmake/dftracer_config_dbg.hpp` files. These files contain the definitions
of configuration options that can be used to control the behavior of the
:code:`dftracer` library. The debug version of the configuration file is used
when the :code:`DFTRACER_DEBUG` macro is defined, allowing for different
configurations in debug builds compared to release builds.

.. code-block:: cpp
   #cmakedefine DFTRACER_MY_OPTION_ENABLE 1

Defining the configuration options is done in the :code:`CMakeLists.txt` file

.. code-block:: cmake
   option(DFTRACER_ENABLE_MY_OPTION "Enable MY_OPTION" ON)
   if (DFTRACER_ENABLE_MY_OPTION)
       set(DFTRACER_MY_OPTION_ENABLE 1)
   endif()

The code can build with the configuration options by setting the
:code:`DFTRACER_ENABLE_MY_OPTION` option in the CMake configuration step

.. code-block:: bash
   cmake -DDFTRACER_ENABLE_MY_OPTION=ON <other options> <path to dftracer source>
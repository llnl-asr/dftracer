===================
Build DFTracer
===================

This section describes how to build DFTracer.

There are three build options:

- build DFTracer with pip (recommended),
- build DFTracer with Spack, and
- build DFTracer with cmake

----------

------------------------------------------
Create Python environment (Recommended)
------------------------------------------
Creating a Python environment is the cleanest way to install DFTracer.

.. code-block:: Bash

    python -m venv <PYTHON-VENV-PATH>
    source <PYTHON-VENV-PATH>/bin/activate


------------------------------------------
Build DFTracer with pip (Recommended)
------------------------------------------

Users can easily install DFTracer using pip. This is the way most python packages are installed.
This method would work for both native python environments and conda environments.


Install DFTracer
*******************************

From PyPI (Recommended)
************************

.. code-block:: Bash

    pip install pydftracer

.. attention::

    For pip installations, all libraries will be present within the site-packages/dftracer/lib.
    This enables clean management of pip installation and uninstallations.

From source
************

.. code-block:: Bash

    git clone git@github.com:LLNL/dftracer.git
    cd dftracer
    # You can skip this for installing the dev branch.
    # for latest stable version use master branch.
    git checkout tags/<Release> -b <Release>
    pip install .

From Github
************

.. code-block:: Bash

  DFT_VERSION=v1.0.4
  pip install git+https://github.com/LLNL/dftracer.git@${DFT_VERSION}

.. attention::

    For pip installations, all libraries will be present within the site-packages/dftracer/lib.
    This enables clean management of pip installation and uninstallations.

-----------------------------------------
Build DFTracer with Spack
-----------------------------------------


One may install DFTracer with Spack_.
If you already have Spack, make sure you have the latest release.
If you use a clone of the Spack develop branch, be sure to pull the latest changes.

.. _build-label:

Install Spack
*************
.. code-block:: Bash

    $ git clone https://github.com/spack/spack
    $ # create a packages.yaml specific to your machine
    $ . spack/share/spack/setup-env.sh

Use `Spack's shell support`_ to add Spack to your ``PATH`` and enable use of the
``spack`` command.

Build and Install DFTracer
*******************************

.. code-block:: Bash

    $ spack install py-pydftracer
    $ spack load py-pydftracer

If the most recent changes on the development branch ('dev') of DFTracer are
desired, then do ``spack install py-pydftracer@develop``.

.. attention::

    The initial install could take a while as Spack will install build
    dependencies (autoconf, automake, m4, libtool, and pkg-config) as well as
    any dependencies of dependencies (cmake, perl, etc.) if you don't already
    have these dependencies installed through Spack or haven't told Spack where
    they are locally installed on your system (i.e., through a custom
    packages.yaml_).
    Run ``spack spec -I py-dftracer-py`` before installing to see what Spack is going
    to do.

----------

------------------------------
Build DFTracer with CMake
------------------------------

Download the latest DFTracer release from the Releases_ page or clone the develop
branch ('develop') from the DFTracer repository
`https://github.com/LLNL/dftracer <https://github.com/LLNL/dftracer>`_.

---------------
Build Variables
---------------

.. table:: section - main build settings using env variables or cmake flags
   :widths: auto

   ================================ ======  ===========================================================================
   Environment Variable             Type    Description
   ================================ ======  ===========================================================================
   DFTRACER_BUILD_TYPE              STRING  Sets the build type for DFTRACER (default Release). Values are Debug or Release
   DFTRACER_ENABLE_FTRACING         BOOL    Enables function tracing (default OFF).
   DFTRACER_ENABLE_MPI              BOOL    Enables MPI Rank (default ON).
   DFTRACER_DISABLE_HWLOC           BOOL    Disables HWLOC (default ON).
   DFTRACER_PYTHON_EXE              STRING  Sets path to python executable. Only Cmake.
   DFTRACER_PYTHON_SITE             STRING  Sets path to python site-packages. Only Cmake.
   DFTRACER_BUILD_PYTHON_BINDINGS   STRING  Enable python bindings for DFTracer. Only Cmake.
   ================================ ======  ===========================================================================

These build variables can be set with cmake as ``-DDISABLE_HWLOC=OFF`` or as environment variables ``export DFTRACER_DISABLE_HWLOC=OFF``

Build DFTracer Dependencies
********************************

The main dependencies DFTracer are
1. cpp-logger : `https://github.com/hariharan-devarajan/cpp-logger.git <https://github.com/hariharan-devarajan/cpp-logger.git>`_ version: 0.0.1
2. gotcha: `https://github.com/LLNL/GOTCHA.git <https://github.com/LLNL/GOTCHA.git>`_ version: develop
3. brahma: `https://github.com/hariharan-devarajan/brahma.git <https://github.com/hariharan-devarajan/brahma.git>`_ version: 0.0.1

These dependencies can be either installed using spack or through cmake from respective respositories.

.. code-block:: Bash
    
    cmake . -B build -DCMAKE_INSTALL_PREFIX=<where you want to install DFTracer>
    cmake --build build
    cmake --install build

-----------

.. explicit external hyperlink targets

.. _Releases: https://github.com/LLNL/dftracer/releases
.. _Spack: https://github.com/spack/spack
.. _Spack's shell support: https://spack.readthedocs.io/en/latest/getting_started.html#add-spack-to-the-shell
.. _packages.yaml: https://spack.readthedocs.io/en/latest/build_settings.html#external-packages

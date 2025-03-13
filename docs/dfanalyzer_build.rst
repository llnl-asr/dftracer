===========================
Build
===========================

------------------------------------------
From source (Recommended)
------------------------------------------

.. code-block:: Bash

    git clone git@github.com:LLNL/dftracer.git
    cd dftracer
    pip install ".[dfanalyzer]"

------------------------------------------
From pip
------------------------------------------

.. code-block:: Bash

    pip install pydftracer[dfanalyzer]

===============================
Getting Started with DFAnalyzer
===============================

The most user-friendly way to utilize DFAnalyzer to analyze traces from DFTracer is to use Jupyter Notebooks.
To run the notebook you will have to install Jupyter. We have a simple requirement.txt file for that as well.

.. code-block:: Bash

    cd dftracer
    pip install -r examples/dfanalyzer/requirements.txt

A simple example of loading DFAnalyzer and quick recommended queries are available on to :code:`<dftracer>/examples/dfanalyzer/dfanalyzer.ipynb` and run your notebook.

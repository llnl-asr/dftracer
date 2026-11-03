================
Debugging
================`


------------
Preload Mode
------------



To enable debugging with the LD_PRELOAD mode, LD_PRELOAD the executable with libdftracer_preload_dbg.so at
runtime instead of the libdftracer_preload.so library


.. code-block:: bash 
    :linenos: 

    # Remaining environment variables as it is 

    # Enable profiler 
    DFTRACER_ENABLE=1

    # Set the mode
    export DFTRACER_INIT=PRELOAD

    # Set the log level ( Supported options: DEBUG, INFO, WARN, ERROR (Default))
    DFTRACER_LOG_LEVEL=DEBUG

    # Run your application
    LD_PRELOAD="$(dftracer-preload-library-path --debug)" ./your_application

.. warning::
    Debug logs set with `DFTRACER_LOG_LEVEL` only work when debugging is enabled

----------------
App Mode
----------------

.. TODO 
.. C/C++

Python:
**************************

.. code-block:: python
   :linenos:

    from dftracer.python.dbg import dftracer, dft_fn
    # Remaining code as it is


Import the logger from dftracer.python.dbg instead of dftracer.python. No other
changes in the code are required to enable debug logging

.. code-block:: bash
   :linenos:

    # Keep the remaining environment variables
    # Enable profiler
    DFTRACER_ENABLE=1
    # Add the log level
    DFTRACER_LOG_LEVEL=DEBUG

    # Run the application
    python3 your_application.py

.. warning::
   Debug logging may impact application performance. Use only for debugging purposes.

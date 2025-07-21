======================
AI/DL Logging
======================

In this document, we detail how to use DFTracer AI Logging style.

----------

----------------------------------------
Motivations
----------------------------------------

Since DFTracer's release, we've successfully traced numerous AI/DL pipelines. 
However, analysis revealed that the resulting traces differ widely across workloads.

This inconsistency is largely due to varied naming schemes used by different users.
Even when the intent is similar, the lack of a standard makes it hard to build analysis tools 
that work reliably across use cases.

This API aims to introduce consistent annotation conventions to help users instrument their code more uniformly. 
With these standards in place, tools like `DFAnalyzer <https://github.com/LLNL/dfanalyzer>`_ can 
operate more effectively — they will *just work™*, reducing fatigue for researchers and 
developers analyzing AI/DL workloads.

----------------------------------------
Import
----------------------------------------

.. code-block:: python

    from dftracer.logger import ai


----------------------------------------
AI/DL Conventions
----------------------------------------

We currently define six categories of logging. Each category, along with its subcategories (children), is implemented as a wrapper around :code:`dft_fn` (see :any:`dft_fn`).

This means you can use these categories (along with its children) in your codebase in the same way you would use :code:`dft_fn` directly.

The table below provides a breakdown of the conventions and how they can be applied in your code:

.. list-table:: AI/DL Logging Conventions
   :widths: 15 15 30 40
   :header-rows: 1

   * - Category
     - Name
     - Access Path
     - Description
   * - Compute
     - Forward
     - ``ai.compute.forward``
     - Forward pass of the network
   * -
     - Backward
     - ``ai.compute.backward``
     - Backward pass / gradient computation
   * -
     - Step
     - ``ai.compute.step``
     - Optimizer step (parameter update)
   * - Data
     - Preprocess
     - ``ai.data.preprocess``
     - Dataset-level preprocessing
   * -
     - Item
     - ``ai.data.item``
     - Per-item transformation or loading
   * - DataLoader
     - Fetch
     - ``ai.dataloader.fetch``
     - Fetch a batch from DataLoader
   * - Comm
     - Send
     - ``ai.comm.send``
     - Point-to-point send
   * -
     - Receive
     - ``ai.comm.receive``
     - Point-to-point receive
   * -
     - Barrier
     - ``ai.comm.barrier``
     - Synchronization barrier
   * -
     - Broadcast
     - ``ai.comm.bcast``
     - Broadcast (one-to-many)
   * -
     - Reduce
     - ``ai.comm.reduce``
     - Reduce (many-to-one)
   * -
     - All-Reduce
     - ``ai.comm.all_reduce``
     - All-reduce (many-to-many)
   * -
     - Gather
     - ``ai.comm.gather``
     - Gather (many-to-one)
   * -
     - All-Gather
     - ``ai.comm.all_gather``
     - All-gather (many-to-many)
   * -
     - Scatter
     - ``ai.comm.scatter``
     - Scatter (one-to-many)
   * -
     - Reduce-Scatter
     - ``ai.comm.reduce_scatter``
     - Reduce-scatter (many-to-many)
   * -
     - All-to-All
     - ``ai.comm.all_to_all``
     - All-to-all (many-to-many)
   * - Device
     - Transfer
     - ``ai.device.transfer``
     - Host-to-device or device-to-host memory transfer
   * - Checkpoint
     - Capture
     - ``ai.checkpoint.capture``
     - Capture a model checkpoint
   * -
     - Restart
     - ``ai.checkpoint.restart``
     - Restart a model checkpoint
   * - Pipeline
     - Epoch
     - ``ai.pipeline.epoch``
     - An entire training or evaluation epoch
   * -
     - Train
     - ``ai.pipeline.train``
     - Training phase
   * -
     - Evaluate
     - ``ai.pipeline.evaluate``
     - Evaluation or validation phase
   * -
     - Test
     - ``ai.pipeline.test``
     - Testing or inference phase



----------------------------------------
Usage
----------------------------------------

To use these conventions, you can annotate your code as follows:

.. code-block:: python

    from dftracer.logger import ai, dftracer

    @ai.compute.forward
    def forward(model, x):
        loss = model(x)
        return loss

    @ai.compute.backward
    def backward(model, loss):
        with ai.comm.all_reduce:
            loss.backward()

    @ai.compute # or @ai.compute.step if you want to be specific
    def compute(model, x, optimizer):
        loss = forward(model, x)
        backward(model, loss)

    @ai.data.preprocess
    def preprocess(data):
        # Preprocessing logic
        pass

    @ai.dataloader.fetch
    def transfer_to_gpu(batch, device):
        batch = batch.to(device)
        pass

    @ai.pipeline.train
    def train(model, dataloader, optimizer, device, num_epoch):
        for epoch in ai.pipeline.epoch.iter(range(num_epoch)):
            for batch in ai.dataloader.fetch.iter(dataloader):
                x, y = transfer_to_gpu(batch, device)
                compute(model, x, optimizer)
                # Additional training logic

    def main():
        model = ...  # Initialize your model
        dataloader = ...  # Initialize your DataLoader
        optimizer = ...  # Initialize your optimizer
        device = ...  # Set your device (CPU/GPU)
        num_epoch = 10  # Set number of epochs

        # initialize dftracer
        df_logger = dftracer.initialize_log(...)
        train(model, dataloader, optimizer, device, num_epoch)
        df_logger.finalize()


----------------------------------------
Extra APIs
----------------------------------------

Context Manager, decorator, and iterable APIs, you name it!
*****************************************

DFTracer AI Logging provides flexible APIs to match different coding styles. 
You can use decorators, context managers, or iterable wrappers to annotate your code cleanly and consistently.

Decorator Style
---------------

* **Without arguments** — use it directly to wrap a function:

.. code-block:: python

    @ai.compute.forward
    def forward(model, x):
        loss = model(x)
        return loss

* **With arguments** — pass metadata to the event:

.. code-block:: python

    @ai.compute.forward(args={"arg1": "value1", "arg2": "value2"})
    def forward(model, x):
        loss = model(x)
        return loss

Context Manager Style
---------------------

Use it to wrap blocks of code inside a `with` statement:

* **Without arguments**:

.. code-block:: python

    with ai.compute.forward:
        loss = model(x)

* **With arguments**:

.. code-block:: python

    with ai.compute.forward(args={"arg1": "value1", "arg2": "value2"}):
        loss = model(x)

Iterable Style
--------------

You can also wrap iterators like data loaders:

.. code-block:: python

    for batch in ai.dataloader.fetch.iter(dataloader):
        ...

Constructor Hooking
-------------------

You can annotate constructors directly using category-specific hooks:

.. code-block:: python

    class MyDataset:
        @ai.data.item.init  # special `init` event for this category
        def __init__(self, ...):
            # Initialization logic
            pass


Updating Arguments
****************************************

Updating arguments is simple. Every profiler (like :code:`ai.compute.forward`) provides an :code:`update` method to 
dynamically change metadata. These updates apply to the entire subtree of that event.

.. code-block:: python

    @ai.compute.forward
    def forward(model, x):
        loss = model(x)
        return loss

    for epoch in ai.pipeline.epoch.iter(range(num_epoch)):
        for step, batch in ai.dataloader.fetch.iter(enumerate(dataloader)):
            # Update metadata for the current context
            ai.compute.forward.update(epoch=epoch, step=step)
            forward(model, batch)


Disabling/Enabling Logging
****************************************

You can turn logging off or on for the entire tree or individual categories:

.. code-block:: python

    # Disable the entire logging tree
    ai.disable()

    # Disable specific categories
    ai.compute.disable()
    ai.data.disable()
    ai.dataloader.disable()
    ai.comm.disable()
    ai.device.disable()
    ai.pipeline.disable()

    # Re-enable specific categories
    ai.compute.enable()
    ai.data.enable()
    ai.dataloader.enable()
    ai.comm.enable()
    ai.device.enable()
    ai.pipeline.enable()

    # Enable everything back
    ai.enable()


Force Enable or Disable Specific Events
****************************************

You can override the global or category-level logging state for individual events by setting the :code:`enable` flag explicitly.

This is useful when you want to selectively trace or skip certain operations, regardless of whether the category 
is globally enabled or disabled.

.. code-block:: python

    ai.compute.disable()  # Disable all compute events

    @ai.compute.forward(enable=True)  # Force-enable this specific event
    def forward(model, x):
        loss = model(x)
        return loss

    with ai.compute.backward(enable=True):  # Force-enable this block
        loss.backward()

    ai.compute.enable()  # Enable all compute events

    @ai.compute.forward(enable=False)  # Force-disable this one
    def forward(model, x):
        loss = model(x)
        return loss

    with ai.compute.backward(enable=False):  # Force-disable this block
        loss.backward()


Updating arguments
****************************************

Updating arguments is simple. Each profiler (like :code:`ai.compute.forward`) has an :code:`update` method 
that lets you change its arguments — similar to how you would pass arguments to :code:`dft_fn`. 
These updates automatically apply to the whole category or its subtree.

Example:

.. code-block:: python

    @ai.compute.forward
    def forward(model, x):
        loss = model(x)
        return loss

    for epoch in ai.pipeline.epoch.iter(range(num_epoch)):
        for step, batch in ai.dataloader.fetch.iter(enumerate(dataloader)):
            # Add context to the forward trace
            ai.compute.forward.update(epoch=epoch, step=step)
            forward(model, batch)


Hook/Checkpoint Style
****************************************

Sometimes you need to attach profilers to hooks (e.g., TensorFlow SessionHook) 
where you can't use decorators or context managers directly.

For these cases, you can manually call the profiler methods:

.. code-block:: python

    class DFTracerProfilingHook(tf.train.SessionRunHook):
        def begin(self):
            self._global_step_tensor = training_util._get_or_create_global_step_read()
            if self._global_step_tensor is None:
                raise RuntimeError("Global step should be created to use ProfilerHook.")
            ai.pipeline.epoch.start()

        def end(self, session):
            ai.pipeline.epoch.stop()
        
        def before_run(self, run_context):
            global_step = run_context.session.run(self._global_step_tensor)
            ai.update(step=global_step)
            ai.compute.start()

        def after_run(self, run_context, run_values):
            ai.compute.stop()

Derivation
****************************************

Since sometimes our logging needs to be more dynamic, you can derive new profilers from existing ones. 
This is useful when you want to create a specialized profiler with the same context as an existing one.

The derived profiler becomes a child of the original profiler, inheriting its context and metadata.
All methods like ``update``, ``enable``, and ``disable`` work on the derived profiler as expected.

Example:

.. code-block:: python

    class Dataset:
        def __getitem__(self, idx: int):
            data = ...
            with ai.data.preprocess:
                # do something with data
                ...
            return data

    # this will become name="preprocess.collate" with cat="data"
    @ai.data.preprocess.derive(name="collate")
    def collate(batch):
        # Collate the batch
        return batch
    
    # OR (context-manager style)

    profiler_collate = ai.data.preprocess.derive(name="collate")

    def collate_fn(batch):
        with profiler_collate:
            return collate(batch)

    # Update
    profiler_collate.update(epoch=epoch)
    # This also works:
    ## this will update all children of ai.data.preprocess
    ## including the derived profiler such as `collate`
    ai.data.preprocess.update(epoch=epoch) 

As metadata / streaming style
****************************************

By default, DFTracer logs events with a start and end time (duration-based logging). 
But sometimes you want to log events immediately as they happen, without waiting for them to finish.

This is useful for real-time monitoring or when you need immediate feedback.

To enable metadata mode, use ``metadata=True``:

.. code-block:: python

    # Regular
    for epoch in ai.pipeline.epoch.iter(range(num_epochs)):
        for step in range(num_steps):
            # Do some work
            ...

    # As metadata
    for epoch in range(num_epochs):
        ai.pipeline.epoch.start(metadata=True)
        for step in range(num_steps):
            # Do some work
        ai.pipeline.epoch.stop(metadata=True)

**Regular mode output:**

.. code-block:: json

    {"id":27,"name":"epoch.block","cat":"pipeline","pid":2877353,"tid":2877353,"ts":1753123213646764,"dur":828765,"ph":"X","args":{"hhash":"2a702c695247d487","p_idx":6,"count":"1","level":2}}
    ...
    {"id":69,"name":"epoch.block","cat":"pipeline","pid":2877353,"tid":2877353,"ts":1753123215361535,"dur":819403,"ph":"X","args":{"hhash":"2a702c695247d487","p_idx":6,"count":"3","level":2}}

**Metadata mode output:**

.. code-block:: json

    {"id":6,"name":"CM","cat":"dftracer","pid":2876815,"tid":2876815,"ph":"M","args":{"hhash":"2a702c695247d487","name":"epoch.end","value":"1753123070219202"}}
    {"id":6,"name":"CM","cat":"dftracer","pid":2876815,"tid":2876815,"ph":"M","args":{"hhash":"2a702c695247d487","name":"epoch.start","value":"1753123070219648"}}
    ...
    {"id":6,"name":"CM","cat":"dftracer","pid":2876815,"tid":2876815,"ph":"M","args":{"hhash":"2a702c695247d487","name":"epoch.end","value":"1753123071041297"}}
    {"id":6,"name":"CM","cat":"dftracer","pid":2876815,"tid":2876815,"ph":"M","args":{"hhash":"2a702c695247d487","name":"epoch.start","value":"1753123071041678"}}

The key difference: metadata mode logs events instantly, while regular mode waits until the event completes to log the duration.

Init event
****************************************

Sometimes you need to log the initialization of a process or component. 

This is useful for tracking startup phases and initialization overhead.

To log an init event, use the ``init`` method:

.. code-block:: python

    class Checkpoint:
        @ai.checkpoint.init
        def __init__(self):
            # Initialize something
            ...

    # or

    with ai.checkpoint.init:
        # Initialize something
        ...

This will output:

.. code-block:: json

    {"id":7,"name":"checkpoint.init","cat":"checkpoint","pid":444541,"tid":444541,"ts":1753136835509693,"dur":100583,"ph":"X","args":{"hhash":"2a702c695247d487","p_idx":6,"level":2}}
    ...

The event name becomes ``checkpoint.init`` (not just ``init``) to avoid conflicts with other events. 
This namespacing keeps events organized under their proper categories.
We could add a separate ``init`` category to our tree, but that would be overkill for something that may not be needed in most codebases.

Caveats
****************************************

**Call ordering matters for enable/disable**

The order of calls can affect whether events get logged or not.

This works as expected:

.. code-block:: python

    class Checkpoint:
        @ai.checkpoint.init # <-- this instance is tracked internally
        def __init__(self):
            # Initialize something
            ...

    if __name__ == "__main__":
        ai.checkpoint.disable()  # Disables all checkpoint events

This syntax sugar doesn't work as expected:

.. code-block:: python

    class Checkpoint:
        @ai.checkpoint.init()  # Notice the parentheses
        def __init__(self):
            ...

    if __name__ == "__main__":
        ai.checkpoint.disable()  # This won't disable the event above

Why?

When you add parentheses ``()``, the decorator creates a new instance immediately. 
Since ``disable()`` is called later, it can't affect the already-created instance.

Solution:

#. Use the decorator without parentheses, or call ``disable()`` before defining your class.
#. Only use parentheses ``()`` when you need to force enable/disable a specific event
#. To add metadata, use the ``update()`` method instead
#. To create variations of an event, use the ``derive()`` method instead
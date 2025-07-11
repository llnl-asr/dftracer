import functools
import sys
from typing import Any, Callable, Iterator, Optional, TypeVar, cast

from dftracer.logger.logger import DFTRACER_ENABLE, dft_fn, dftracer

if sys.version_info >= (3, 11):
    from enum import StrEnum as StringEnum, auto
    # StrEnum in 3.11 already lower-case
else:
    from enum import Enum, auto

    # MIT License: https://github.com/irgeek/StrEnum/blob/master/strenum/__init__.py
    class StringEnum(str, Enum):
        # this is a specific version that will lowercase the values

        def __new__(cls, value, *args, **kwargs):
            if not isinstance(value, (str, auto)):
                raise TypeError(
                    f"Values of StrEnums must be strings: {value!r} is a {type(value)}"
                )
            return super().__new__(cls, value, *args, **kwargs)

        def __str__(self):
            return str(self.value)

        def _generate_next_value_(name, *_):
            return name.lower()


def get_iter_block_name(name: str):
    return f"{name}.block" if not name.endswith(".block") else name


def get_iter_handle_name(name: str):
    return f"{name}.iter" if not name.endswith(".iter") else name


F = TypeVar("F", bound=Callable[..., Any])

ITER_COUNT_NAME = "count"
INIT_NAME = "init"


class DFTracerAI:
    def __init__(self, cat: str, name: Optional[str] = None):
        self.profiler = dft_fn(
            cat=cat,
            name=name,
        )

    def __call__(
        self,
        fn: Optional[F] = None,
        enable: bool = True,
        epoch: Optional[int] = None,
        step: Optional[int] = None,
        image_idx: Optional[int] = None,
        image_size=None,
        args=None,
    ):
        if epoch is not None:
            self.profiler._arguments["epoch"] = str(epoch)
        if step is not None:
            self.profiler._arguments["step"] = str(step)
        if image_idx is not None:
            self.profiler._arguments["image_idx"] = str(image_idx)
        if image_size is not None:
            self.profiler._arguments["image_size"] = str(image_size)
        args = args or {}
        for key, value in args.items():
            self._arguments[key] = str(value)

        if fn:

            def _decorator(f):
                @functools.wraps(f)
                def wrapper(*args, **kwargs):
                    if enable:
                        with self.profiler:
                            return f(*args, **kwargs)
                    return f(*args, **kwargs)

                return wrapper

            return cast(F, _decorator(fn))
        else:
            return cast(F, DFTracerAI())

    def __enter__(self):
        self.profiler.__enter__()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.profiler.__exit__(exc_type, exc_val, exc_tb)
        return False

    def update(self, epoch=None, step=None, image_idx=None, image_size=None, args=None):
        if args is None:
            args = {}
        if DFTRACER_ENABLE and self.profiler._enable:
            if epoch is not None:
                self.profiler._arguments["epoch"] = str(epoch)
            if step is not None:
                self.profiler._arguments["step"] = str(step)
            if image_idx is not None:
                self.profiler._arguments["image_idx"] = str(image_idx)
            if image_size is not None:
                self.profiler._arguments["image_size"] = str(image_size)
            for key, value in args.items():
                self.profiler._arguments[key] = str(value)
        return self

    def log_init(self, fn):
        return self.profiler.log_init(
            name=f"{self.profiler._name}.{INIT_NAME}", f_py=fn
        )

    def iter(
        self,
        iterator: Iterator,
        *,
        include_block: bool = True,
        include_iter: bool = True,
        iter_name: Optional[str] = None,
        block_name: Optional[str] = None,
    ):
        iter_name = iter_name or get_iter_handle_name(self.profiler._name)
        block_name = block_name or get_iter_block_name(self.profiler._name)
        iter_val = 1

        start: int = 0
        if DFTRACER_ENABLE and self.profiler._enable:
            self.profiler_arguments = {}
            start = dftracer.get_instance().get_time()

        for v in iterator:
            if DFTRACER_ENABLE and self.profiler._enable:
                end = dftracer.get_instance().get_time()
                t0 = dftracer.get_instance().get_time()

            yield v

            if DFTRACER_ENABLE and self.profiler._enable:
                t1 = dftracer.get_instance().get_time()
                self.profiler._arguments[ITER_COUNT_NAME] = str(iter_val)
                args = (
                    self.profiler._arguments
                    if len(self.profiler._arguments) > 0
                    else None
                )

                if include_iter:
                    dftracer.get_instance().enter_event()
                    dftracer.get_instance().log_event(
                        name=iter_name,
                        cat=self.profiler._cat,
                        start_time=start,
                        duration=end - start,
                        string_args=args,
                    )
                    dftracer.get_instance().exit_event()

                if include_block:
                    dftracer.get_instance().enter_event()
                    dftracer.get_instance().log_event(
                        name=block_name,
                        cat=self.profiler._cat,
                        start_time=t0,
                        duration=t1 - t0,
                        string_args=args,
                    )
                    dftracer.get_instance().exit_event()

                iter_val += 1
                start = dftracer.get_instance().get_time()


# Enumerations


class ProfileCategory(StringEnum):
    COMPUTE = auto()
    DATA = auto()
    DATALOADER = auto()
    COMM = auto()
    DEVICE = auto()
    PIPELINE = auto()


class ComputeEvent(StringEnum):
    FORWARD = auto()
    BACKWARD = auto()
    STEP = auto()


class DataEvent(StringEnum):
    PREPROCESS = auto()
    ITEM = auto()


class DataLoaderEvent(StringEnum):
    FETCH = auto()


# Terminologies are taken from https://docs.pytorch.org/docs/stable/distributed.html
class CommunicationEvent(StringEnum):
    SEND = auto()
    RECEIVE = auto()
    BARRIER = auto()
    BCAST = auto()
    REDUCE = auto()
    ALL_REDUCE = auto()
    GATHER = auto()
    ALL_GATHER = auto()
    SCATTER = auto()
    REDUCE_SCATTER = auto()
    ALL_TO_ALL = auto()


class DeviceEvent(StringEnum):
    TRANSFER = auto()


class PipelineEvent(StringEnum):
    EPOCH = auto()
    TRAIN = auto()
    EVALUATE = auto()
    TEST = auto()


class _Compute:
    forward = DFTracerAI(cat=ProfileCategory.COMPUTE, name=ComputeEvent.FORWARD)
    backward = DFTracerAI(cat=ProfileCategory.COMPUTE, name=ComputeEvent.BACKWARD)
    step = DFTracerAI(cat=ProfileCategory.COMPUTE, name=ComputeEvent.STEP)


class _Data:
    preprocess = DFTracerAI(cat=ProfileCategory.DATA, name=DataEvent.PREPROCESS)
    item = DFTracerAI(cat=ProfileCategory.DATA, name=DataEvent.ITEM)


class _DataLoader:
    fetch = DFTracerAI(cat=ProfileCategory.DATALOADER, name=DataLoaderEvent.FETCH)


class _Communication:
    send = DFTracerAI(cat=ProfileCategory.COMM, name=CommunicationEvent.SEND)
    receive = DFTracerAI(cat=ProfileCategory.COMM, name=CommunicationEvent.RECEIVE)
    barrier = DFTracerAI(cat=ProfileCategory.COMM, name=CommunicationEvent.BARRIER)
    bcast = DFTracerAI(cat=ProfileCategory.COMM, name=CommunicationEvent.BCAST)
    reduce = DFTracerAI(cat=ProfileCategory.COMM, name=CommunicationEvent.REDUCE)
    all_reduce = DFTracerAI(
        cat=ProfileCategory.COMM, name=CommunicationEvent.ALL_REDUCE
    )
    gather = DFTracerAI(cat=ProfileCategory.COMM, name=CommunicationEvent.GATHER)
    all_gather = DFTracerAI(
        cat=ProfileCategory.COMM, name=CommunicationEvent.ALL_GATHER
    )
    scatter = DFTracerAI(cat=ProfileCategory.COMM, name=CommunicationEvent.SCATTER)
    reduce_scatter = DFTracerAI(
        cat=ProfileCategory.COMM, name=CommunicationEvent.REDUCE_SCATTER
    )
    all_to_all = DFTracerAI(
        cat=ProfileCategory.COMM, name=CommunicationEvent.ALL_TO_ALL
    )


class _Device:
    transfer = DFTracerAI(cat=ProfileCategory.DEVICE, name=DeviceEvent.TRANSFER)


class _Pipeline:
    epoch = DFTracerAI(cat=ProfileCategory.PIPELINE, name=PipelineEvent.EPOCH)
    train = DFTracerAI(cat=ProfileCategory.PIPELINE, name=PipelineEvent.TRAIN)
    evaluate = DFTracerAI(cat=ProfileCategory.PIPELINE, name=PipelineEvent.EVALUATE)
    test = DFTracerAI(cat=ProfileCategory.PIPELINE, name=PipelineEvent.TEST)


class _AI:
    compute = _Compute()
    data = _Data()
    dataloader = _DataLoader()
    comm = _Communication()
    device = _Device()
    pipeline = _Pipeline()


ai = _AI()
comm = ai.comm
compute = ai.compute
data = ai.data
dataloader = ai.dataloader
device = ai.device
pipeline = ai.pipeline

__all__ = [
    "CommunicationEvent",
    "ComputeEvent",
    "DataEvent",
    "DataLoaderEvent",
    "DeviceEvent",
    "ai",
    "comm",
    "compute",
    "data",
    "dataloader",
    "device",
    "pipeline",
]

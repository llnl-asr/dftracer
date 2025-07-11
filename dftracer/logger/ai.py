import functools
import sys
from typing import Any, Optional, Callable, TypeVar, cast

from dftracer.logger.logger import dft_fn, DFTRACER_ENABLE, dftracer


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


def make_wrapper(cat: str, name: Optional[str] = None):
    if name is None:
        name = cat

    class _DFTWrapper:
        def __init__(
            self,
            func=None,
            *,
            epoch=None,
            step=None,
            image_idx=None,
            image_size=None,
            enable=True,
        ):
            self.profiler = dft_fn(
                cat=cat,
                name=name,
                epoch=epoch,
                step=step,
                image_idx=image_idx,
                image_size=image_size,
                enable=enable,
            )
            self._name = name
            self._cat = cat
            self._epoch = epoch
            self._step = step
            self._image_idx = image_idx
            self._image_size = image_size
            self._enable = enable
            self.func = func

            if callable(func):
                functools.update_wrapper(self, func)

        def __call__(self, *args, **kwargs):
            @functools.wraps(self.func)
            def wrapper(*args, **kwargs):
                with self.profiler:
                    return self.func(*args, **kwargs)

            return wrapper(*args, **kwargs)

        def __enter__(self):
            self.profiler.__enter__()
            return self

        def __exit__(self, exc_type, exc_val, exc_tb):
            self.profiler.__exit__(exc_type, exc_val, exc_tb)
            return False

        def __iter__(self):
            return self.iter()

        def update(
            self, epoch=None, step=None, image_idx=None, image_size=None, args={}
        ):
            if DFTRACER_ENABLE and self._enable:
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

        def log_init(self):
            return functools.partial(
                self.profiler.log_init, name=f"{self._name}.{INIT_NAME}"
            )

        def iter(
            self,
            include_block: bool = True,
            include_iter: bool = True,
            iter_name: Optional[str] = None,
            block_name: Optional[str] = None,
        ):
            if self.func is None:
                raise ValueError("Function or iterable is not defined.")

            iter_name = iter_name or get_iter_handle_name(self._name)
            block_name = block_name or get_iter_block_name(self._name)
            iter_val = 1

            start: int = 0
            if DFTRACER_ENABLE and self._enable:
                self.profiler_arguments = {}
                start = dftracer.get_instance().get_time()

            iterable = self.func() if callable(self.func) else self.func

            for v in iterable:
                if DFTRACER_ENABLE and self._enable:
                    end = dftracer.get_instance().get_time()
                    t0 = dftracer.get_instance().get_time()

                yield v

                if DFTRACER_ENABLE and self._enable:
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
                            cat=self._cat,
                            start_time=start,
                            duration=end - start,
                            string_args=args,
                        )
                        dftracer.get_instance().exit_event()

                    if include_block:
                        dftracer.get_instance().enter_event()
                        dftracer.get_instance().log_event(
                            name=block_name,
                            cat=self._cat,
                            start_time=t0,
                            duration=t1 - t0,
                            string_args=args,
                        )
                        dftracer.get_instance().exit_event()

                    iter_val += 1
                    start = dftracer.get_instance().get_time()

    # support @decorator(...) syntax
    class WrapperFactory(_DFTWrapper):
        def __call__(
            self,
            func: Optional[F] = None,
            *,
            epoch=None,
            step=None,
            image_idx=None,
            image_size=None,
            enable=True,
            **kwargs,
        ):
            return _DFTWrapper(
                func=func,
                epoch=epoch,
                step=step,
                image_idx=image_idx,
                image_size=image_size,
                enable=enable,
                **kwargs,
            )

    def dec(
        _=None,
        fn: Optional[F] = None,
        *,
        epoch=None,
        step=None,
        image_idx=None,
        image_size=None,
        enable=True,
        **kwargs,
    ):
        if fn is None:
            return cast(
                F,
                WrapperFactory(
                    epoch=epoch,
                    step=step,
                    image_idx=image_idx,
                    image_size=image_size,
                    enable=enable,
                    **kwargs,
                ),
            )
        return cast(
            F,
            _DFTWrapper(
                fn,
                epoch=epoch,
                step=step,
                image_idx=image_idx,
                image_size=image_size,
                enable=enable,
                **kwargs,
            ),
        )

    return dec


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
    forward = make_wrapper(cat=ProfileCategory.COMPUTE, name=ComputeEvent.FORWARD)
    backward = make_wrapper(cat=ProfileCategory.COMPUTE, name=ComputeEvent.BACKWARD)
    step = make_wrapper(cat=ProfileCategory.COMPUTE, name=ComputeEvent.STEP)


class _Data:
    preprocess = make_wrapper(cat=ProfileCategory.DATA, name=DataEvent.PREPROCESS)
    ITEM = make_wrapper(cat=ProfileCategory.DATA, name=DataEvent.ITEM)


class _DataLoader:
    fetch = make_wrapper(cat=ProfileCategory.DATALOADER, name=DataLoaderEvent.FETCH)


class _Communication:
    send = make_wrapper(cat=ProfileCategory.COMM, name=CommunicationEvent.SEND)
    receive = make_wrapper(cat=ProfileCategory.COMM, name=CommunicationEvent.RECEIVE)
    barrier = make_wrapper(cat=ProfileCategory.COMM, name=CommunicationEvent.BARRIER)
    bcast = make_wrapper(cat=ProfileCategory.COMM, name=CommunicationEvent.BCAST)
    reduce = make_wrapper(cat=ProfileCategory.COMM, name=CommunicationEvent.REDUCE)
    all_reduce = make_wrapper(
        cat=ProfileCategory.COMM, name=CommunicationEvent.ALL_REDUCE
    )
    gather = make_wrapper(cat=ProfileCategory.COMM, name=CommunicationEvent.GATHER)
    all_gather = make_wrapper(
        cat=ProfileCategory.COMM, name=CommunicationEvent.ALL_GATHER
    )
    scatter = make_wrapper(cat=ProfileCategory.COMM, name=CommunicationEvent.SCATTER)
    reduce_scatter = make_wrapper(
        cat=ProfileCategory.COMM, name=CommunicationEvent.REDUCE_SCATTER
    )
    all_to_all = make_wrapper(
        cat=ProfileCategory.COMM, name=CommunicationEvent.ALL_TO_ALL
    )


class _Device:
    transfer = make_wrapper(cat=ProfileCategory.DEVICE, name=DeviceEvent.TRANSFER)


class _Pipeline:
    epoch = make_wrapper(cat=ProfileCategory.PIPELINE, name=PipelineEvent.EPOCH)
    train = make_wrapper(cat=ProfileCategory.PIPELINE, name=PipelineEvent.TRAIN)
    evaluate = make_wrapper(cat=ProfileCategory.PIPELINE, name=PipelineEvent.EVALUATE)
    test = make_wrapper(cat=ProfileCategory.PIPELINE, name=PipelineEvent.TEST)


class _AI:
    compute = _Compute()
    data = _Data()
    dataloader = _DataLoader()
    communication = _Communication()
    device = _Device()
    pipeline = _Pipeline()


ai = _AI()
compute = ai.compute
data = ai.data
dataloader = ai.dataloader
communication = ai.communication
device = ai.device

__all__ = [
    "CommunicationEvent",
    "ComputeEvent",
    "DataEvent",
    "DataLoaderEvent",
    "DeviceEvent",
    "ai",
    "communication",
    "compute",
    "data",
    "dataloader",
    "device",
]

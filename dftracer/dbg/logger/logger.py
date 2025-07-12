from functools import wraps
from typing import Dict
import os
import logging

DFTRACER_ENABLE_ENV = "DFTRACER_ENABLE"
DFTRACER_INIT_ENV = "DFTRACER_INIT"
DFTRACER_LOG_LEVEL_ENV = "DFTRACER_LOG_LEVEL"

DFTRACER_ENABLE = True if os.getenv(DFTRACER_ENABLE_ENV, '0') == '1' else False
DFTRACER_INIT_PRELOAD = True if os.getenv(DFTRACER_INIT_ENV, 'PRELOAD') == 'PRELOAD' else False
DFTRACER_LOG_LEVEL = os.getenv(DFTRACER_LOG_LEVEL_ENV, 'ERROR')

from pathlib import Path
import inspect
import sys, signal

if DFTRACER_ENABLE:
    import pydftracer_dbg as profiler

def capture_signal(signal_number, frame):
    dftracer.get_instance().finalize()
    sys.exit(signal_number)

if DFTRACER_ENABLE:
    signal.signal(signal.SIGABRT, capture_signal)
    signal.signal(signal.SIGINT, capture_signal)
    signal.signal(signal.SIGTERM, capture_signal)


def setup_logger(name, log_file, formatter, level=logging.INFO):
    """To setup as many loggers as you want"""
    handler = logging.FileHandler(log_file)
    handler.setFormatter(logging.Formatter(formatter))
    logger = logging.getLogger(name)
    logger.setLevel(level)
    logger.addHandler(handler)
    return logger

class dftracer:
    __instance = None

    def __init__(self):
        if DFTRACER_ENABLE:
            self.logger = None
            self.dbg_logging = None
        dftracer.__instance = self

    @classmethod
    def get_instance(cls):
        """ Static access method. """
        if dftracer.__instance is None:
            dftracer()
        return dftracer.__instance

    @staticmethod
    def initialize_log(logfile, data_dir, process_id):
        log_file_path = None
        if logfile:
            log_file_path = Path(logfile)
        outfile = "dft.log"
        if DFTRACER_ENABLE:
            if log_file_path:
                os.makedirs(log_file_path.parent, exist_ok=True)
                outfile = os.path.join(log_file_path.parent, "dft.log")
        log_level = logging.ERROR
        if DFTRACER_LOG_LEVEL == "DEBUG":
            log_level = logging.DEBUG
        elif DFTRACER_LOG_LEVEL == "INFO":
            log_level = logging.INFO
        elif DFTRACER_LOG_LEVEL == "WARN":
            log_level = logging.WARN
        instance = dftracer.get_instance()
        instance.dbg_logging = setup_logger(name="dftracer_dbg", log_file=outfile, formatter='[DFTRACER_PY %(levelname)s] %(message)s [%(pathname)s:%(lineno)d]', level=log_level)
        instance.dbg_logging.debug(f"logger.initialize_log {logfile} {data_dir} {process_id}")
        if DFTRACER_ENABLE:
            instance.logger = profiler
            instance.dbg_logging.debug(f"logger.initialize {logfile} {data_dir} {process_id}")
            instance.logger.initialize(log_file=logfile, data_dirs=data_dir, process_id=process_id)
        return instance

    def get_time(self):
        if DFTRACER_ENABLE and self.logger:
            t = self.logger.get_time()
            self.dbg_logging.debug(f"logger.get_time {t}")
            return t
        return 0

    def enter_event(self):
        if DFTRACER_ENABLE and self.logger:
            self.logger.enter_event()
            self.dbg_logging.debug(f"logger.enter_event")

    def exit_event(self):
        if DFTRACER_ENABLE and self.logger:
            self.logger.exit_event()
            self.dbg_logging.debug(f"logger.exit_event")

    def log_event(self, name, cat, start_time, duration, string_args=None):
        if DFTRACER_ENABLE and self.logger:
            self.dbg_logging.debug(f"logger.log_event {name} {cat} {start_time} {duration} {string_args}")
            if string_args is None:
                string_args = {}
            self.logger.log_event(name=name, cat=cat, start_time=start_time, duration=duration, string_args=string_args)

    def log_metadata_event(self, key, value):
        if DFTRACER_ENABLE and self.logger:
            self.dbg_logging.debug(f"logger.log_metadata_event {key} {value}")
            if string_args is None:
                string_args = {}
            self.logger.log_metadata_event(key=key, value=value)

    def finalize(self):
        if DFTRACER_ENABLE and self.logger:
            self.dbg_logging.debug(f"logger.finalize")
            self.logger.finalize()

def get_default_args(func):
    signature = inspect.signature(func)
    return {
        k: v.default
        for k, v in signature.parameters.items()
        if v.default is not inspect.Parameter.empty
    }

class dft_fn(object):

    def __init__(self, cat, name=None, epoch=None, step=None, image_idx=None, image_size=None, enable=True):
        self._enable = enable
        if DFTRACER_ENABLE and self._enable:
            if not name:
                name = inspect.stack()[1].function
            self._name = name
            self._cat = cat
            self._arguments: Dict[str, str] = {}
            if epoch is not None: self._arguments["epoch"] = str(epoch)
            if step is not None: self._arguments["step"] = str(step)
            if image_idx is not None: self._arguments["image_idx"] = str(image_idx)
            if image_size is not None: self._arguments["image_size"] = str(image_size)
            self.reset()

    def __enter__(self):
        if DFTRACER_ENABLE and self._enable:
            self._t1 = dftracer.get_instance().get_time()
            dftracer.get_instance().enter_event()
        return self

    def update(self, epoch=None, step=None, image_idx=None, image_size=None, args={}):
        if DFTRACER_ENABLE and self._enable:
            if epoch is not None: self._arguments["epoch"] = str(epoch)
            if step is not None: self._arguments["step"] = str(step)
            if image_idx is not None: self._arguments["image_idx"] = str(image_idx)
            if image_size is not None: self._arguments["image_size"] = str(image_size)
            for key, value in args.items():
                self._arguments[key] = str(value)
        return self

    def flush(self):
        if DFTRACER_ENABLE and self._enable:
            self._t2 = dftracer.get_instance().get_time()
            if len(self._arguments) > 0:
                dftracer.get_instance().log_event(name=self._name, cat=self._cat, start_time=self._t1,
                                                     duration=self._t2 - self._t1,
                                                     string_args=self._arguments)
            else:
                dftracer.get_instance().log_event(name=self._name, cat=self._cat, start_time=self._t1,
                                                     duration=self._t2 - self._t1)
            dftracer.get_instance().exit_event()
            self._flush = True
        return self

    def reset(self):
        if DFTRACER_ENABLE and self._enable:
            self._t1 = dftracer.get_instance().get_time()
            dftracer.get_instance().enter_event()
            self._t2 = self._t1
            self._flush = False
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if DFTRACER_ENABLE and self._enable:
            if not self._flush:
                self.flush()

    def log(self, func):
        if DFTRACER_ENABLE and self._enable:
            arg_names = inspect.getfullargspec(func)[0]
            self._arguments = {}

        @wraps(func)
        def wrapper(*args, **kwargs):
            if DFTRACER_ENABLE and self._enable:
                if len(arg_names) > 0:
                    if "self" == arg_names[0]:
                        if hasattr(args[0], "epoch"):
                            self._arguments["epoch"] = str(args[0].epoch)
                        if hasattr(args[0], "step"):
                            self._arguments["step"] = str(args[0].step)
                        if hasattr(args[0], "image_size"):
                            self._arguments["image_size"] = str(args[0].image_size)
                        if hasattr(args[0], "image_idx"):
                            self._arguments["image_idx"] = str(args[0].image_idx)
                    full_args = dict(zip(arg_names[1:], args[1:]))
                    full_args.update(kwargs)
                    full_args.update(get_default_args(func))

                    for name, value in full_args.items():
                        if name == "epoch":
                            self._arguments["epoch"] = str(value)
                        elif name == "image_idx":
                            self._arguments["image_idx"] = str(value)
                        elif name == "image_size":
                            self._arguments["image_size"] = str(value)
                        elif name == "step":
                            self._arguments["step"] = str(value)

                start = dftracer.get_instance().get_time()
                dftracer.get_instance().enter_event()
            x = func(*args, **kwargs)
            if DFTRACER_ENABLE and self._enable:
                end = dftracer.get_instance().get_time()
                if len(self._arguments) > 0:
                    dftracer.get_instance().log_event(name=func.__qualname__, cat=self._cat, start_time=start,
                                                         duration=end - start,
                                                         string_args=self._arguments)
                else:
                    dftracer.get_instance().log_event(name=func.__qualname__, cat=self._cat, start_time=start,
                                                         duration=end - start)
                dftracer.get_instance().exit_event()
            return x

        return wrapper

    def log_metadata(self, key, value):
        if DFTRACER_ENABLE and self._enable:
            dftracer.get_instance().log_metadata_event(key=key, value=value)

    def iter(self, func, name="loop", iter_name="step"):
        if DFTRACER_ENABLE and self._enable:
            iter_val = 1
            _name = f"{name}.iter"
            kernal_name = f"{name}.yield"
            start = dftracer.get_instance().get_time()
            self._arguments = {}

        for v in func:
            if DFTRACER_ENABLE and self._enable:
                end = dftracer.get_instance().get_time()
                t0 = dftracer.get_instance().get_time()
            yield v
            if DFTRACER_ENABLE and self._enable:
                t1 = dftracer.get_instance().get_time()
                self._arguments[iter_name] = str(iter_val)
                if len(self._arguments) > 0:
                    dftracer.get_instance().enter_event()
                    dftracer.get_instance().log_event(name=_name, cat=self._cat, start_time=start,
                                                         duration=end - start,
                                                         string_args=self._arguments)
                    dftracer.get_instance().exit_event()
                    dftracer.get_instance().enter_event()
                    dftracer.get_instance().log_event(name=kernal_name, cat=self._cat, start_time=t0,
                                                         duration=t1 - t0,
                                                         string_args=self._arguments)
                    dftracer.get_instance().exit_event()
                else:
                    dftracer.get_instance().enter_event()
                    dftracer.get_instance().log_event(name=_name, cat=self._cat, start_time=start,
                                                         duration=end - start)
                    dftracer.get_instance().exit_event()
                    dftracer.get_instance().enter_event()
                    dftracer.get_instance().log_event(name=kernal_name, cat=self._cat, start_time=t0,
                                                         duration=t1 - t0)
                    dftracer.get_instance().exit_event()

                iter_val += 1
                start = dftracer.get_instance().get_time()

    def log_init(self, init):
        if DFTRACER_ENABLE and self._enable:
            arg_names = inspect.getfullargspec(init)[0]
            self._arguments = {}

        @wraps(init)
        def new_init(*args, **kwargs):
            if DFTRACER_ENABLE and self._enable:
                arg_values = dict(zip(arg_names[1:], args))
                arg_values.update(kwargs)
                arg_values.update(get_default_args(init))
                if "epoch" in arg_values:
                    self._arguments["epoch"] = str(arg_values["epoch"])
                elif "image_idx" in arg_values:
                    self._arguments["image_idx"] = str(arg_values["image_idx"])
                elif "image_size" in arg_values:
                    self._arguments["image_size"] = str(arg_values["image_size"])
                elif "step" in arg_values:
                    self._arguments["step"] = str(arg_values["step"])
                #self._arguments = {k: str(v).replace("\n", "") for k, v in arg_values.items()} # enforce string for all values
                start = dftracer.get_instance().get_time()
                dftracer.get_instance().enter_event()
            init(*args, **kwargs)
            if DFTRACER_ENABLE and self._enable:
                end = dftracer.get_instance().get_time()

                if len(self._arguments) > 0:
                    dftracer.get_instance().log_event(name=init.__qualname__, cat=self._cat, start_time=start,
                                                         duration=end - start,
                                                         string_args=self._arguments)
                else:
                    dftracer.get_instance().log_event(name=init.__qualname__, cat=self._cat, start_time=start,
                                                         duration=end - start)
                dftracer.get_instance().exit_event()
        return new_init

    def log_static(self, func):

        @wraps(func)
        def wrapper(*args, **kwargs):
            if DFTRACER_ENABLE and self._enable:
                start = dftracer.get_instance().get_time()
                dftracer.get_instance().enter_event()
            x = func(*args, **kwargs)
            if DFTRACER_ENABLE and self._enable:
                end = dftracer.get_instance().get_time()
                if len(self._arguments) > 0:
                    dftracer.get_instance().log_event(name=func.__qualname__, cat=self._cat, start_time=start,
                                                         duration=end - start,
                                                         string_args=self._arguments)
                else:
                    dftracer.get_instance().log_event(name=func.__qualname__, cat=self._cat, start_time=start,
                                                         duration=end - start)
                dftracer.get_instance().exit_event()
            return x

        return wrapper

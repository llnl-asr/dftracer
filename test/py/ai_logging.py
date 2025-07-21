import argparse
import os
from time import sleep

import numpy as np

from dftracer.logger import dftracer, ai


class IOHandler:
    @ai.data.item
    def read(self, filename):
        return np.load(filename)

    def write(self, filename, a):
        with open(filename, "wb") as f:
            np.save(f, a)


def get_args():
    parser = argparse.ArgumentParser(
        prog="DFTracer AI Logging testing",
    )
    parser.add_argument(
        "--log_dir",
        default="./pfw_logs",
        type=str,
        help="The log directory to save to the tracing",
    )
    parser.add_argument(
        "--data_dir",
        default="./data",
        type=str,
        help="The directory to save and load data",
    )
    parser.add_argument(
        "--disable-ai-cat",
        choices=["all", "dataloader", "device", "compute", "comm", "ckpt"],
        default=None,
        type=str,
        help="Disable AI category",
    )
    parser.add_argument("--num_files", default=1, type=int, help="Number of files")
    parser.add_argument(
        "--niter", default=1, type=int, help="Number of iterations for the experiment"
    )
    parser.add_argument(
        "--epoch-as-metadata", action="store_true", default=False,
        help="Use epoch as metadata in the AI logger",
    )
    parser.add_argument(
        "--record_size",
        default=1048576,
        type=int,
        help="size of the record to be written to the file",
    )
    args = parser.parse_args()
    return args


def data_gen(args, io, data):
    for i in range(args.num_files):
        io.write(f"{args.data_dir}/npz/{i}-of-{args.num_files}.npy", data)


@ai.dataloader.fetch
def read_data(args, io, epoch):
    for i in range(args.num_files):
        yield io.read(f"{args.data_dir}/npz/{i}-of-{args.num_files}.npy")

@ai.data.preprocess.derive(name="collate")
def collate(data):
    return data

@ai.device.transfer
def transfer(data):
    sleep(0.1)
    return data


@ai.compute.forward
def forward(data):
    sleep(0.1)
    return 0.0


@ai.compute.backward
def backward():
    sleep(0.1)
    with ai.comm.all_reduce(enable=False):
        sleep(0.1)


@ai.compute
def compute(data):
    _ = forward(data)
    backward()
    return _

class Checkpoint:
    @ai.checkpoint.init
    def __init__(self):
        sleep(0.1)

    @ai.checkpoint.capture
    def capture(self, _):
        sleep(0.1)
        return _
    
    @ai.checkpoint.restart
    def restart(self, _):
        sleep(0.1)
        return _

class Hook:
    def before_step(self, *args, **kwargs):
        ai.compute.step.start()

    def after_step(self, *args, **kwargs):
        ai.compute.step.stop()


@ai.pipeline.train
def train(args, hook):
    io = IOHandler()

    os.makedirs(f"{args.log_dir}/npz", exist_ok=True)
    os.makedirs(f"{args.data_dir}/npz", exist_ok=True)
    data = np.ones((args.record_size, 1), dtype=np.uint8)
    data_gen(args, io, data)

    checkpoint = Checkpoint()

    checkpoint.restart({})

    if args.epoch_as_metadata:
        for epoch in range(args.niter):
            ai.pipeline.epoch.start(metadata=True)
            for step, data in ai.dataloader.fetch.iter(
                enumerate(read_data(args, io, epoch))
            ):
                hook.before_step()
                data = collate(data)
                _ = transfer(data)
                _ = compute(data)
                hook.after_step()
                ai.dataloader.fetch.update(step=step, epoch=epoch)
            ai.pipeline.epoch.stop(metadata=True)
    else:
        for epoch in ai.pipeline.epoch.iter(range(args.niter)):
            for step, data in ai.dataloader.fetch.iter(
                enumerate(read_data(args, io, epoch))
            ):
                hook.before_step()
                data = collate(data)
                _ = transfer(data)
                _ = compute(data)
                hook.after_step()
                ai.dataloader.fetch.update(step=step, epoch=epoch)

    checkpoint.capture({})

def main():
    args = get_args()

    if args.disable_ai_cat == "all":
        ai.disable()
    elif args.disable_ai_cat == "dataloader":
        ai.dataloader.disable()
    elif args.disable_ai_cat == "device":
        ai.device.disable()
    elif args.disable_ai_cat == "compute":
        ai.compute.disable()
    elif args.disable_ai_cat == "comm":
        ai.comm.disable()
    elif args.disable_ai_cat == "ckpt":
        ai.checkpoint.disable()

    hook = Hook()
    df_logger = dftracer.initialize_log(logfile=None, data_dir=None, process_id=-1)
    train(args, hook)
    df_logger.finalize()


if __name__ == "__main__":
    main()

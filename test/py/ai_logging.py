from time import sleep
import argparse
import os

from dftracer.logger import dftracer
from dftracer.logger.ai import ai

import numpy as np


class IOHandler:
    def read(self, filename):
        return np.load(filename)

    def write(self, filename, a):
        with open(filename, "wb") as f:
            np.save(f, a)


def get_args():
    parser = argparse.ArgumentParser(
        prog="DFTracer testing",
        description="What the program does",
        epilog="Text at the bottom of help",
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
    parser.add_argument("--num_files", default=1, type=int, help="Number of files")
    parser.add_argument(
        "--niter", default=1, type=int, help="Number of iterations for the experiment"
    )
    parser.add_argument(
        "--record_size",
        default=1048576,
        type=int,
        help="size of the record to be written to the file",
    )
    args = parser.parse_args()
    return args


def data_gen(args, io: IOHandler, data):
    for i in range(args.num_files):
        io.write(f"{args.data_dir}/{i}-of-{args.num_files}.npz", data)


@ai.dataloader.fetch
def read_data(args, io: IOHandler, epoch):
    for i in range(args.num_files):
        yield io.read(f"{args.data_dir}/npz/{i}-of-{args.num_files}.npz")


@ai.device.transfer
def transfer(data):
    sleep(2)


def f2():
    with ai.device.transfer(enable=False):
        sleep(2)


def f3():
    for i in ai.dataloader.fetch(range(5)):
        print(f"F3 {i}")
        sleep(2)


def f4():
    for i in ai.dataloader.fetch(range(5)).iter(
        include_block=False, include_iter=False, iter_name="f4"
    ):
        print(f"F4 {i}")


def main():
    args = get_args()
    io = IOHandler()

    os.makedirs(f"{args.log_dir}/npz", exist_ok=True)
    os.makedirs(f"{args.data_dir}/npz", exist_ok=True)
    data = np.ones((args.record_size, 1), dtype=np.uint8)
    data_gen(args, io, data)

    df_logger = dftracer.initialize_log(f"{args.log_dir}_npz.pfw", None, -1)
    for epoch in ai.pipeline.epoch(range(args.niter)):
        for step in ai.dataloader.fetch(read_data(args, io, epoch)):
            ai.dataloader.fetch.update(step=step, epoch=epoch)
    df_logger.finalize()


if __name__ == "__main__":
    main()

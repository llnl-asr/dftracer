import argparse
import os
import threading
from multiprocessing import get_context
from time import sleep

import numpy as np
import PIL.Image as im
from dftracer.python import dftracer, dft_fn
import resource

cwd = os.getcwd()
log_file = os.getenv("LOG_FILE", f"{cwd}/test_py-app.pwf")
log_inst = dftracer.initialize_log(logfile=None, data_dir=None, process_id=-1)
dft_fn = dft_fn("COMPUTE")

parser = argparse.ArgumentParser(
    prog='DFTracer testing',
    description='What the program does',
    epilog='Text at the bottom of help')
parser.add_argument("--data_dir", default="./data", type=str,
                    help="The directory to save and load data")
parser.add_argument("--format", default="npz",
                    type=str, help="format of the file")
parser.add_argument("--num_files", default=1, type=int, help="Number of files")
parser.add_argument("--niter", default=1, type=int,
                    help="Number of iterations for the experiment")
parser.add_argument("--record_size", default=1048576, type=int,
                    help="size of the record to be written to the file")
args = parser.parse_args()
data_dir = args.data_dir
os.makedirs(f"{args.data_dir}/{args.format}", exist_ok=True)


@dft_fn.log
def log_events(index):
    sleep(0.001)


def custom_events():
    log_inst.enter_event()
    start = log_inst.get_time()
    sleep(0.001)
    end = log_inst.get_time()
    log_inst.log_event("test", "cat2", start, end - start)
    log_inst.exit_event()
    for i in dft_fn.iter(range(2)):
        sleep(0.001)


def posix_calls(val):
    index, is_spawn = val
    path = f"{data_dir}/demofile{index}.txt"
    f = open(path, "w+")
    f.write("Now the file has more content!")
    f.close()
    if is_spawn:
        print(f"Calling spawn on {index} with pid {os.getpid()}")
        resource.setrlimit(resource.RLIMIT_NOFILE, (256, 256))
    else:
        print(f"Not calling spawn on {index} with pid {os.getpid()}")

    log_inst.finalize()
    os._exit(0)


def npz_calls(index):
    # print(f"{cwd}/data/demofile2.npz")
    path = f"{data_dir}/demofile{index}.npz"
    if os.path.exists(path):
        os.remove(path)
    records = np.random.randint(255, size=(8, 8, 1024), dtype=np.uint8)
    record_labels = [0] * 1024
    np.savez(path, x=records, y=record_labels)


def jpeg_calls(index):
    records = np.random.randint(255, size=(1024, 1024), dtype=np.uint8)
    img = im.fromarray(records)
    out_path_spec = f"{data_dir}/test.jpeg"
    img.save(out_path_spec, format="JPEG", bits=8)
    with open(out_path_spec, "rb") as f:
        image = im.open(f)
        out_records = np.asarray(image)
    # image = im.open(out_path_spec)


@dft_fn.log
def data_gen(num_files, data_dir, format, data):
    for i in dft_fn.iter(range(num_files)):
        i = IOWriter(
            filename=f"{data_dir}/{format}/{i}-of-{num_files}.{format}",
            format=format,
            data=data,
        )


@dft_fn.log
def read_data(num_files, data_dir, format):
    for i in dft_fn.iter(range(num_files)):
        io = IOReader(
            filename=f"{data_dir}/{format}/{i}-of-{num_files}.{format}",
            format=format,
        )
        d = io.get()


class IOHandler:
    def __init__(self, filename, format):
        self.format = format
        self.filename = filename


class IOReader(IOHandler):
    @dft_fn.log_init
    def __init__(self, filename, format):
        super().__init__(filename=filename, format=format)
        if self.format == "jpeg" or self.format == "png":
            self.data = np.asarray(im.open(filename))
        if self.format == "npz":
            self.data = np.load(filename)
        if self.format == "hdf5":
            import h5py

            fd = h5py.File(filename, "r")
            self.data = fd["x"][:]  # type: ignore
            fd.close()

    def get(self):
        return self.data


class IOWriter(IOHandler):
    @dft_fn.log_init
    def __init__(self, filename, format, data):
        super().__init__(filename=filename, format=format)
        if self.format == "jpeg" or self.format == "png":
            image = im.fromarray(data)
            # im.show()
            image.save(filename)
        if self.format == "npz":
            with open(filename, "wb") as f:
                np.save(f, data)
        if self.format == "hdf5":
            import h5py

            fd = h5py.File(filename, "w")
            fd.create_dataset("x", data=data)
            fd.close()


def init():
    """This function is called when new processes start."""
    print(f"Initializing process {os.getpid()}")


@dft_fn.log
def with_default_args(step=2):
    for i in dft_fn.iter(range(step)):
        print(i)


def main():
    dft_fn.log_metadata("key", "value")
    posix_calls((20, False))
    t1 = threading.Thread(target=posix_calls, args=((10, False),))
    custom_events()
    t2 = threading.Thread(target=npz_calls, args=(1,))
    t3 = threading.Thread(target=jpeg_calls, args=(2,))
    t4 = threading.Thread(target=log_events, args=(3,))
    # starting thread 1
    t1.start()
    t2.start()
    t3.start()
    t4.start()

    t1.join()
    t2.join()
    t3.join()
    t4.join()
    index = 4
    with get_context("fork").Pool(1, initializer=init) as pool:
        pool.map(posix_calls, ((index, False),))
    index = index + 1

    with get_context("spawn").Pool(1, initializer=init) as pool:
        pool.map(posix_calls, ((index, True),))
    index = index + 1

    # testing named parameters
    data = np.ones((args.record_size, 1), dtype=np.uint8)
    data_gen(num_files=args.num_files, data_dir=args.data_dir,
             format=args.format, data=data)
    for n in range(args.niter):
        read_data(num_files=args.num_files,
                  data_dir=args.data_dir, format=args.format)

    with_default_args()

    log_inst.finalize()


if __name__ == "__main__":
    main()

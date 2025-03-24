from importlib.metadata import version, PackageNotFoundError

try:
    __version__ = version("pydftracer")
except PackageNotFoundError:
    # package is not installed
    pass

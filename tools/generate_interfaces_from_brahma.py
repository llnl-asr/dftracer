#!/usr/bin/env python3
"""
Generate DFTracer Brahma stubs from Brahma-generated interface headers.

This script parses Brahma interface headers (hdf5/mpi/mpiio) with clang,
collects virtual methods across macro contexts, and emits DFTracer header/cpp
files with override declarations and forwarding wrappers.
"""

from __future__ import annotations

import argparse
import glob
import os
import re
import shlex
import shutil
import subprocess
import tempfile
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Callable, Dict, Iterable, List, Optional, Sequence, Set, Tuple

import clang.cindex as cix


@dataclass(frozen=True)
class ParseContext:
    version: int
    impl_macro: Optional[str] = None


@dataclass(frozen=True)
class MethodKey:
    name: str
    return_type: str
    arg_types: Tuple[str, ...]


@dataclass
class MethodInfo:
    key: MethodKey
    args: List[str]
    arg_types: List[str]
    arg_names: List[str]
    return_kind: cix.TypeKind
    contexts: Set[ParseContext] = field(default_factory=set)


@dataclass(frozen=True)
class InterfaceSpec:
    name: str
    base_class: str
    tracer_class: str
    enable_macro: str
    version_macro: str
    system_header: str
    category: str
    trace_type: str
    mpi_interface: bool


SPECS: Dict[str, InterfaceSpec] = {
    "hdf5": InterfaceSpec(
        name="hdf5",
        base_class="HDF5",
        tracer_class="HDF5DFTracer",
        enable_macro="BRAHMA_ENABLE_HDF5",
        version_macro="BRAHMA_HDF5_VERSION",
        system_header="hdf5.h",
        category="HDF5",
        trace_type="TRACE_TYPE_HDF5",
        mpi_interface=False,
    ),
    "mpi": InterfaceSpec(
        name="mpi",
        base_class="MPI",
        tracer_class="MPIDFTracer",
        enable_macro="BRAHMA_ENABLE_MPI",
        version_macro="BRAHMA_MPI_VERSION",
        system_header="mpi.h",
        category="MPI",
        trace_type="TRACE_TYPE_MPI",
        mpi_interface=True,
    ),
    "mpiio": InterfaceSpec(
        name="mpiio",
        base_class="MPIIO",
        tracer_class="MPIIODFTracer",
        enable_macro="BRAHMA_ENABLE_MPI",
        version_macro="BRAHMA_MPI_VERSION",
        system_header="mpi.h",
        category="MPIIO",
        trace_type="TRACE_TYPE_MPI",
        mpi_interface=True,
    ),
}

NO_LOG_METHODS: Dict[str, Set[str]] = {
    # DFTLogger internally queries MPI state; wrapping these with
    # DFT_LOGGER_START_ALWAYS() causes recursive interception.
    "mpi": {"MPI_Initialized", "MPI_Finalized"},
}

# Sub-categories within a single `type`, grouping functions by the MPI
# standard chapter they belong to (point-to-point, collective, RMA, ...)
# so a trace reader can tell e.g. a Bcast from a Send without decoding args.
MPI_CATEGORY_RULES: List[Tuple[str, str]] = [
    (r"^MPI_Win_", "rma"),
    (r"^MPI_R(get|put)(_accumulate)?(_c)?$", "rma"),
    (r"^MPI_Raccumulate(_c)?$", "rma"),
    (r"^MPI_(Put|Get|Accumulate|Get_accumulate)(_c)?$", "rma"),
    (r"^MPI_Fetch_and_op(_c)?$", "rma"),
    (r"^MPI_Compare_and_swap$", "rma"),

    (r"^MPI_I?Bcast", "collective"),
    (r"^MPI_I?Allreduce", "collective"),
    (r"^MPI_I?Reduce", "collective"),
    (r"^MPI_I?Allgather", "collective"),
    (r"^MPI_I?Gather", "collective"),
    (r"^MPI_I?Scatter", "collective"),
    (r"^MPI_I?Alltoall", "collective"),
    (r"^MPI_I?Scan", "collective"),
    (r"^MPI_I?Exscan", "collective"),
    (r"^MPI_I?Barrier", "collective"),
    (r"^MPI_I?Neighbor_", "collective"),
    (r"^MPI_Op_", "collective"),

    (r"^MPI_Cart", "topology"),
    (r"^MPI_Graph", "topology"),
    (r"^MPI_Dist_graph", "topology"),
    (r"^MPI_Dims_create$", "topology"),
    (r"^MPI_Topo_test$", "topology"),

    (r"^MPI_Type_", "datatype"),
    (r"^MPI_Pack", "datatype"),
    (r"^MPI_Unpack", "datatype"),
    (r"^MPI_Get_address$", "datatype"),
    (r"^MPI_Address$", "datatype"),
    (r"^MPI_Aint_", "datatype"),

    (r"^MPI_(Comm_spawn|Comm_accept|Comm_connect|Comm_disconnect|Comm_join|Comm_get_parent)", "spawn"),
    (r"^MPI_(Open_port|Close_port|Publish_name|Unpublish_name|Lookup_name)$", "spawn"),

    (r"^MPI_(Comm|Intercomm)_", "comm"),
    (r"^MPI_Group_", "comm"),
    (r"^MPI_Keyval_", "comm"),
    (r"^MPI_Attr_", "comm"),
    (r"^MPI_Errhandler_", "comm"),

    (r"^MPI_Info_", "metadata"),
    (r"^MPI_Register_datarep", "metadata"),

    (r"^MPI_I?[BRS]?send", "p2p"),
    (r"^MPI_I?M?recv", "p2p"),
    (r"^MPI_P(ready|send_init|recv_init|arrived)", "p2p"),
    (r"^MPI_(Wait|Test|Start|Cancel|Request_|Status_|I?M?probe|Buffer_|Grequest_|Is_thread_main)", "p2p"),
]


def mpi_category(name: str) -> str:
    for pattern, category in MPI_CATEGORY_RULES:
        if re.match(pattern, name, re.IGNORECASE):
            return category
    return "env"


def hdf5_category(name: str) -> str:
    # HDF5's own naming convention already groups functions by module
    # (H5F* file, H5D* dataset, H5T* datatype, ...); reuse that letter.
    match = re.match(r"^H5([A-Z])", name)
    if match:
        return "h5" + match.group(1).lower()
    return "h5"


PER_FUNCTION_CATEGORY: Dict[str, Callable[[str], str]] = {
    "mpi": mpi_category,
    "hdf5": hdf5_category,
}


def normalize_ws(value: str) -> str:
    return re.sub(r"\s+", " ", value.strip())


def array_decl_from_type(arg_type: cix.Type, name: str) -> Tuple[str, str]:
    """Return declaration text and normalized type text for array parameters."""
    suffixes: List[str] = []
    current = arg_type
    while current.kind in (cix.TypeKind.CONSTANTARRAY, cix.TypeKind.INCOMPLETEARRAY):
        if current.kind == cix.TypeKind.CONSTANTARRAY:
            suffixes.append(f"[{current.element_count}]")
        else:
            suffixes.append("[]")
        current = current.element_type

    base_type = normalize_ws(current.spelling)
    suffix = "".join(suffixes)
    arg_type_text = f"{base_type}{suffix}"
    decl_text = f"{base_type} {name}{suffix}"
    return decl_text, arg_type_text


def extract_versions(header_text: str, version_macro: str) -> List[int]:
    values = {
        int(match.group(1))
        for match in re.finditer(rf"{re.escape(version_macro)}\s*>=\s*(\d+)", header_text)
    }
    return sorted(values)


def extract_impl_macros(header_text: str) -> List[str]:
    values = set(re.findall(r"defined\((BRAHMA_MPI_IMPL_[A-Z0-9_]+)\)", header_text))
    return sorted(values)


def next_minor(version: int) -> int:
    major = version // 100000
    minor = (version % 100000) // 100
    return major * 100000 + (minor + 1) * 100


def merge_ranges(ranges: Iterable[Tuple[int, int]]) -> List[Tuple[int, int]]:
    ordered = sorted(ranges)
    if not ordered:
        return []
    merged: List[Tuple[int, int]] = [ordered[0]]
    for start, end in ordered[1:]:
        last_start, last_end = merged[-1]
        if start <= last_end:
            merged[-1] = (last_start, max(last_end, end))
        else:
            merged.append((start, end))
    return merged


def build_version_condition(version_macro: str, versions: Sequence[int]) -> Optional[str]:
    if not versions:
        return None
    ranges = merge_ranges((v, next_minor(v)) for v in sorted(set(versions)))
    pieces = [f"({version_macro} >= {start} && {version_macro} < {end})" for start, end in ranges]
    if len(pieces) == 1:
        return pieces[0]
    return f"({' || '.join(pieces)})"


def build_condition(spec: InterfaceSpec, contexts: Set[ParseContext]) -> Optional[str]:
    if not contexts:
        return None
    if not spec.mpi_interface:
        versions = [ctx.version for ctx in contexts]
        return build_version_condition(spec.version_macro, versions)

    by_impl: Dict[str, List[int]] = {}
    for ctx in contexts:
        if ctx.impl_macro is None:
            continue
        by_impl.setdefault(ctx.impl_macro, []).append(ctx.version)

    if not by_impl:
        return None

    impl_pieces = []
    for impl_macro in sorted(by_impl):
        version_cond = build_version_condition(spec.version_macro, by_impl[impl_macro])
        if version_cond:
            impl_pieces.append(f"(defined({impl_macro}) && {version_cond})")
        else:
            impl_pieces.append(f"defined({impl_macro})")

    if len(impl_pieces) == 1:
        return impl_pieces[0]
    return f"({' || '.join(impl_pieces)})"


def wrap_if(condition: Optional[str], body: str) -> str:
    if not condition:
        return f"{body}\n"
    return f"#if {condition}\n{body}\n#endif\n"


def should_log_arg(arg_type: str) -> bool:
    arg_type = normalize_ws(arg_type)
    if "*" in arg_type or "[" in arg_type:
        return False
    integer_markers = [
        "int",
        "size_t",
        "ssize_t",
        "off_t",
        "off64_t",
        "long",
        "short",
        "unsigned",
        "uint",
        "int8_t",
        "int16_t",
        "int32_t",
        "int64_t",
        "uint8_t",
        "uint16_t",
        "uint32_t",
        "uint64_t",
        "MPI_Count",
    ]
    if any(marker in arg_type for marker in integer_markers):
        return True

    mpi_handles = [
        "MPI_Comm",
        "MPI_Datatype",
        "MPI_Request",
        "MPI_Status",
        "MPI_Info",
        "MPI_Op",
        "MPI_Win",
        "MPI_Errhandler",
        "MPI_Group",
        "MPI_Message",
        "MPI_File",
        "MPI_Aint",
        "MPI_Offset",
        "MPI_Fint",
    ]
    if any(handle in arg_type for handle in mpi_handles):
        return True

    hdf5_handles = [
        "hid_t",
        "herr_t",
        "hsize_t",
        "hssize_t",
        "haddr_t",
        "hbool_t",
        "htri_t",
        "H5I_type_t",
    ]
    if any(handle in arg_type for handle in hdf5_handles):
        return True

    return "enum" in arg_type.lower()


def discover_libclang_path(cli_path: Optional[str]) -> str:
    if cli_path:
        return cli_path

    search_patterns = [
        "/usr/lib64/libclang.so*",
        "/usr/lib/llvm-*/lib/libclang.so*",
        "/usr/lib*/libclang.so*",
        str(Path(__file__).resolve().parent.parent / ".venv/lib/python*/site-packages/clang/native/libclang.so*"),
    ]
    candidates: List[str] = []
    for pattern in search_patterns:
        candidates.extend(glob.glob(pattern))
    candidates = sorted(set(candidates))
    if not candidates:
        raise RuntimeError("Could not auto-discover libclang. Pass --libclang-path.")
    return candidates[-1]


def include_dirs_from_command(cmd: List[str]) -> List[str]:
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, check=False)
    except FileNotFoundError:
        return []

    raw = result.stdout.strip() if result.stdout.strip() else result.stderr.strip()
    tokens = shlex.split(raw)
    dirs = []
    for token in tokens:
        if token.startswith("-I") and len(token) > 2:
            dirs.append(token[2:])

    # Some wrappers (e.g. mpicc --showme:incdirs) return plain include dirs
    # without -I prefixes.
    if not dirs and raw:
        for token in tokens:
            if token.startswith("/") and os.path.isdir(token):
                dirs.append(token)
    return dirs


def clang_system_include_dirs() -> List[str]:
    try:
        result = subprocess.run(
            ["clang", "-E", "-x", "c", "-", "-v"],
            input="",
            capture_output=True,
            text=True,
            check=False,
        )
    except FileNotFoundError:
        return []

    dirs: List[str] = []
    in_block = False
    for line in result.stderr.splitlines():
        if "#include <...> search starts here:" in line:
            in_block = True
            continue
        if in_block and "End of search list." in line:
            break
        if in_block:
            path = line.strip()
            if path and not path.startswith("(framework"):
                dirs.append(path)
    return dirs


def clang_resource_include_dir() -> List[str]:
    try:
        resource = subprocess.check_output(["clang", "-print-resource-dir"], text=True).strip()
    except (FileNotFoundError, subprocess.SubprocessError):
        return []
    include_path = os.path.join(resource, "include")
    return [include_path] if os.path.isdir(include_path) else []


def build_include_dirs(
    brahma_include_root: Path,
    workspace_root: Path,
    extra_include_dirs: Sequence[str] = (),
) -> List[str]:
    dirs = [
        str(brahma_include_root),
        str(workspace_root / "install/include"),
        str(workspace_root / "build/src/gotcha/include"),
        str(workspace_root / "build/src/gotcha-build/include"),
    ]
    # Caller-supplied dirs go BEFORE auto-detected ones so build-system paths
    # (e.g., the exact MPI/HDF5 include dirs CMake's find_package picked) win
    # over h5cc/mpicc auto-discovery, which fails silently on some distros
    # (e.g., Ubuntu mpich's h5cc -show returns no -I flags). When typedefs
    # like hid_t can't be resolved, libclang falls back to the underlying
    # type and the generated overrides won't match brahma's parent class.
    dirs.extend(extra_include_dirs)
    mpi_inc_dirs = include_dirs_from_command(["mpicc", "--showme:incdirs"])
    dirs.extend(mpi_inc_dirs)
    if not mpi_inc_dirs:
        dirs.extend(include_dirs_from_command(["mpicc", "-show"]))
    dirs.extend(include_dirs_from_command(["h5cc", "-show"]))
    dirs.extend(clang_resource_include_dir())
    dirs.extend(clang_system_include_dirs())

    deduped: List[str] = []
    seen: Set[str] = set()
    for path in dirs:
        if not path:
            continue
        norm = os.path.abspath(path)
        if norm in seen or not os.path.isdir(norm):
            continue
        seen.add(norm)
        deduped.append(norm)
    return deduped


def make_shim_brahma_config(tmp_dir: Path) -> Path:
    shim_root = tmp_dir / "brahma"
    shim_root.mkdir(parents=True, exist_ok=True)
    cfg = shim_root / "brahma_config.hpp"
    cfg.write_text(
        "#pragma once\n"
        "#ifndef BRAHMA_ENABLE_HDF5\n#define BRAHMA_ENABLE_HDF5 1\n#endif\n"
        "#ifndef BRAHMA_ENABLE_MPI\n#define BRAHMA_ENABLE_MPI 1\n#endif\n"
        "#ifndef BRAHMA_HDF5_VERSION\n#define BRAHMA_HDF5_VERSION 0\n#endif\n"
        "#ifndef BRAHMA_MPI_VERSION\n#define BRAHMA_MPI_VERSION 0\n#endif\n"
    )
    return tmp_dir


def find_virtual_methods(
    index: cix.Index,
    spec: InterfaceSpec,
    header_path: Path,
    class_name: str,
    include_dirs: Sequence[str],
    defines: Sequence[str],
) -> List[Tuple[cix.Cursor, List[str], List[str], List[str]]]:
    args = ["-x", "c++", "-std=c++17"] + [f"-I{inc}" for inc in include_dirs] + [f"-D{d}" for d in defines]
    if header_path.name == "hdf5.h":
        pass
    tu = index.parse(str(header_path), args=args)

    out: List[Tuple[cix.Cursor, List[str], List[str], List[str]]] = []
    for ns in tu.cursor.get_children():
        if ns.kind != cix.CursorKind.NAMESPACE or ns.spelling != "brahma":
            continue
        for cls in ns.get_children():
            if cls.kind != cix.CursorKind.CLASS_DECL or cls.spelling != class_name:
                continue
            for method in cls.get_children():
                if method.kind != cix.CursorKind.CXX_METHOD or not method.is_virtual_method():
                    continue
                if spec.mpi_interface and method.spelling.endswith(("_f2c", "_c2f")):
                    continue

                args_decl: List[str] = []
                arg_types_raw: List[str] = []
                arg_names: List[str] = []
                for arg_idx, arg in enumerate(method.get_arguments()):
                    name = arg.spelling or f"arg{arg_idx}"
                    if name == "result":
                        name = "result2"
                    if arg.type.kind in (cix.TypeKind.CONSTANTARRAY, cix.TypeKind.INCOMPLETEARRAY):
                        arg_decl, arg_type = array_decl_from_type(arg.type, name)
                        args_decl.append(arg_decl)
                    else:
                        arg_type = normalize_ws(arg.type.spelling)
                        args_decl.append(f"{arg_type} {name}")
                    arg_types_raw.append(arg_type)
                    arg_names.append(name)

                if not args_decl:
                    args_decl = ["void"]
                    arg_types_raw = ["void"]
                    arg_names = []

                out.append((method, args_decl, arg_names, arg_types_raw))
    return out


def build_contexts(spec: InterfaceSpec, header_text: str) -> List[ParseContext]:
    versions = extract_versions(header_text, spec.version_macro)
    if not versions:
        return []
    if not spec.mpi_interface:
        return [ParseContext(version=v) for v in versions]

    impl_macros = extract_impl_macros(header_text)
    if not impl_macros:
        return [ParseContext(version=v) for v in versions]

    contexts: List[ParseContext] = []
    for impl in impl_macros:
        for version in versions:
            contexts.append(ParseContext(version=version, impl_macro=impl))
    return contexts


def collect_methods_for_interface(
    index: cix.Index,
    spec: InterfaceSpec,
    header_path: Path,
    include_dirs: Sequence[str],
    verbose: bool = False,
) -> Dict[MethodKey, MethodInfo]:
    header_text = header_path.read_text(errors="ignore")
    contexts = build_contexts(spec, header_text)
    if verbose:
        print(f"[{spec.name}] contexts: {len(contexts)}")
    methods: Dict[MethodKey, MethodInfo] = {}

    if not contexts:
        return methods

    for idx, ctx in enumerate(contexts, start=1):
        defines = [spec.enable_macro, f"{spec.version_macro}={ctx.version}"]
        if ctx.impl_macro:
            defines.append(ctx.impl_macro)

        found = find_virtual_methods(index, spec, header_path, spec.base_class, include_dirs, defines)
        if verbose:
            impl_name = ctx.impl_macro or "-"
            print(f"[{spec.name}] context {idx}/{len(contexts)} v={ctx.version} impl={impl_name}: {len(found)} methods")

        for cursor, args_decl, arg_names, arg_types_raw in found:
            arg_types = tuple(normalize_ws(arg_type) for arg_type in arg_types_raw)
            key = MethodKey(
                name=cursor.spelling,
                return_type=normalize_ws(cursor.result_type.spelling),
                arg_types=arg_types,
            )
            info = methods.get(key)
            if info is None:
                info = MethodInfo(
                    key=key,
                    args=args_decl,
                    arg_types=list(arg_types_raw),
                    arg_names=arg_names,
                    return_kind=cursor.result_type.kind,
                )
                methods[key] = info
            info.contexts.add(ctx)
    return methods


def method_decl_line(info: MethodInfo) -> str:
    args = ", ".join(info.args)
    return f"  {info.key.return_type} {info.key.name}({args}) override;"


def method_impl_block(spec: InterfaceSpec, info: MethodInfo) -> str:
    args = ", ".join(info.args)
    arg_call = ", ".join(info.arg_names)
    skip_logging = info.key.name in NO_LOG_METHODS.get(spec.name, set())

    category_fn = PER_FUNCTION_CATEGORY.get(spec.name)
    category_line = (
        f'  ConstEventNameType CATEGORY = "{category_fn(info.key.name)}";\n'
        if category_fn and not skip_logging
        else ""
    )

    if skip_logging:
        if info.return_kind == cix.TypeKind.VOID:
            return (
                f"void brahma::{spec.tracer_class}::{info.key.name}({args}) {{\n"
                f"  BRAHMA_MAP_OR_FAIL({info.key.name});\n"
                f"  __real_{info.key.name}({arg_call});\n"
                f"}}"
            )

        return (
            f"{info.key.return_type} brahma::{spec.tracer_class}::{info.key.name}({args}) {{\n"
            f"  BRAHMA_MAP_OR_FAIL({info.key.name});\n"
            f"  return __real_{info.key.name}({arg_call});\n"
            f"}}"
        )

    log_lines = []
    for arg_type, arg_name in zip(info.arg_types, info.arg_names):
        if arg_type == "void":
            continue
        if should_log_arg(arg_type):
            log_lines.append(f"  DFT_LOGGER_UPDATE_TYPE({arg_name}, MetadataType::MT_VALUE);")
    log_block = "\n".join(log_lines)
    if log_block:
        log_block += "\n"

    if info.return_kind == cix.TypeKind.VOID:
        return (
            f"void brahma::{spec.tracer_class}::{info.key.name}({args}) {{\n"
            f"{category_line}"
            f"  BRAHMA_MAP_OR_FAIL({info.key.name});\n"
            f"  DFT_LOGGER_START_ALWAYS();\n"
            f"{log_block}"
            f"  __real_{info.key.name}({arg_call});\n"
            f"  DFT_LOGGER_END();\n"
            f"}}"
        )

    return (
        f"{info.key.return_type} brahma::{spec.tracer_class}::{info.key.name}({args}) {{\n"
        f"{category_line}"
        f"  BRAHMA_MAP_OR_FAIL({info.key.name});\n"
        f"  DFT_LOGGER_START_ALWAYS();\n"
        f"{log_block}"
        f"  {info.key.return_type} ret = __real_{info.key.name}({arg_call});\n"
        f"  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);\n"
        f"  DFT_LOGGER_END();\n"
        f"  return ret;\n"
        f"}}"
    )


def generate_header(spec: InterfaceSpec, methods: Sequence[MethodInfo], timestamp: str) -> str:
    guard = f"DFTRACER_{spec.base_class}_H"

    decls: List[str] = []
    for method in methods:
        cond = build_condition(spec, method.contexts)
        decls.append(wrap_if(cond, method_decl_line(method)))

    return (
        "///\n"
        f"/// This file is generated by tools/generate_interfaces_from_brahma.py\n"
        f"/// Generated on: {timestamp}\n"
        "///\n"
        "/// This is a dftracer stub file. Regenerate and customize as needed.\n"
        "///\n\n"
        f"#ifndef {guard}\n"
        f"#define {guard}\n\n"
        "#include <brahma/brahma.h>\n"
        "#include <dftracer/core/common/constants.h>\n"
        "#include <dftracer/core/common/logging.h>\n"
        "#include <dftracer/core/common/typedef.h>\n\n"
        f"#ifdef {spec.enable_macro}\n"
        f"#include <{spec.system_header}>\n\n"
        "#include <dftracer/core/df_logger.h>\n\n"
        "namespace brahma {\n\n"
        f"class {spec.tracer_class} : public {spec.base_class} {{\n"
        " private:\n"
        f"  static std::shared_ptr<{spec.tracer_class}> instance;\n"
        "  static bool stop_trace;\n"
        "  std::shared_ptr<DFTLogger> logger;\n\n"
        " public:\n"
        f"  {spec.tracer_class}() : {spec.base_class}() {{ logger = DFT_LOGGER_INIT(); }}\n\n"
        f"  virtual ~{spec.tracer_class}() {{}}\n\n"
        f"  static std::shared_ptr<{spec.tracer_class}> get_instance() {{\n"
        "    if (!stop_trace && instance == nullptr) {\n"
        f"      instance = std::make_shared<{spec.tracer_class}>();\n"
        f"      {spec.base_class}::set_instance(instance);\n"
        "    }\n"
        "    return instance;\n"
        "  }\n\n"
        "  void finalize() { stop_trace = true; }\n\n"
        + "".join(decls)
        + "};\n"
        "}  // namespace brahma\n\n"
        f"#endif // {spec.enable_macro}\n"
        f"#endif // {guard}\n"
    )


def generate_cpp(spec: InterfaceSpec, methods: Sequence[MethodInfo], timestamp: str) -> str:
    blocks: List[str] = []
    for method in methods:
        cond = build_condition(spec, method.contexts)
        blocks.append(wrap_if(cond, method_impl_block(spec, method)))

    category_static = (
        ""
        if spec.name in PER_FUNCTION_CATEGORY
        else f'static ConstEventNameType CATEGORY = "{spec.category}";\n'
    )

    return (
        "///\n"
        f"/// This file is generated by tools/generate_interfaces_from_brahma.py\n"
        f"/// Generated on: {timestamp}\n"
        "///\n"
        "/// This is a dftracer stub file. Regenerate and customize as needed.\n"
        "///\n\n"
        f"#include <dftracer/core/brahma/{spec.name}.h>\n"
        f"#ifdef {spec.enable_macro}\n\n"
        f"{category_static}"
        f"static TraceEventType TRACE_TYPE = TraceEventType::{spec.trace_type};\n\n"
        f"std::shared_ptr<brahma::{spec.tracer_class}> brahma::{spec.tracer_class}::instance = nullptr;\n"
        f"bool brahma::{spec.tracer_class}::stop_trace = false;\n\n"
        + "".join(blocks)
        + f"\n#endif // {spec.enable_macro}\n"
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate DFTracer stubs from Brahma interface headers")
    parser.add_argument(
        "--workspace-root",
        type=Path,
        default=Path(__file__).resolve().parent.parent,
        help="Workspace root path",
    )
    parser.add_argument(
        "--brahma-include-root",
        type=Path,
        default=None,
        help="Root include path that contains brahma/interface/*.h (default: <workspace>/build/src/brahma/include)",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=None,
        help="Output dir for generated files (default: <workspace>/src/dftracer/core/brahma)",
    )
    parser.add_argument(
        "--interfaces",
        nargs="+",
        choices=sorted(SPECS.keys()),
        default=["hdf5", "mpi", "mpiio"],
        help="Interfaces to generate",
    )
    parser.add_argument(
        "--libclang-path",
        type=str,
        default=None,
        help="Path to libclang shared library",
    )
    parser.add_argument(
        "--extra-include-dir",
        action="append",
        default=[],
        dest="extra_include_dirs",
        metavar="DIR",
        help=(
            "Additional include dir to pass to libclang (repeatable). "
            "Use this to inject the exact MPI/HDF5 paths CMake found, e.g. "
            "via $<TARGET_PROPERTY:hdf5,INTERFACE_INCLUDE_DIRECTORIES>; "
            "needed because h5cc/mpicc auto-discovery is unreliable on some "
            "distros (Ubuntu mpich) and unresolved typedefs silently degrade "
            "to their underlying type."
        ),
    )
    parser.add_argument("--verbose", action="store_true", help="Verbose output")
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    workspace_root = args.workspace_root.resolve()
    brahma_include_root = (
        args.brahma_include_root.resolve()
        if args.brahma_include_root
        else (workspace_root / "build/src/brahma/include").resolve()
    )
    output_dir = (
        args.output_dir.resolve()
        if args.output_dir
        else (workspace_root / "src/dftracer/core/brahma").resolve()
    )
    output_dir.mkdir(parents=True, exist_ok=True)

    libclang_path = discover_libclang_path(args.libclang_path)
    cix.Config.set_library_file(libclang_path)
    clang_format_exe = shutil.which("clang-format")
    if args.verbose:
        print(f"[config] libclang: {libclang_path}")
        print(f"[config] brahma include root: {brahma_include_root}")
        print(f"[config] output dir: {output_dir}")
        print(
            "[config] clang-format: "
            + (clang_format_exe if clang_format_exe else "not found (formatting disabled)")
        )

    if not clang_format_exe:
        print("[warn] clang-format not found in PATH; generated files will not be formatted.")

    include_dirs = build_include_dirs(
        brahma_include_root,
        workspace_root,
        extra_include_dirs=args.extra_include_dirs,
    )
    index = cix.Index.create()
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    with tempfile.TemporaryDirectory(prefix="dftracer_brahma_gen_") as tmp:
        shim_root = make_shim_brahma_config(Path(tmp))
        include_dirs_with_shim = [str(shim_root)] + include_dirs

        for name in args.interfaces:
            spec = SPECS[name]
            header_path = brahma_include_root / "brahma/interface" / f"{name}.h"
            if not header_path.exists():
                print(f"[{name}] missing source header: {header_path}")
                return 1

            methods_map = collect_methods_for_interface(
                index,
                spec,
                header_path,
                include_dirs_with_shim,
                verbose=args.verbose,
            )

            methods = sorted(methods_map.values(), key=lambda m: (m.key.name, m.key.return_type, m.key.arg_types))
            if args.verbose:
                print(f"[{name}] unique methods: {len(methods)}")

            header_out = output_dir / f"{name}.h"
            cpp_out = output_dir / f"{name}.cpp"
            header_out.write_text(generate_header(spec, methods, timestamp))
            cpp_out.write_text(generate_cpp(spec, methods, timestamp))
            print(f"[{name}] generated: {header_out} ({len(methods)} methods)")
            print(f"[{name}] generated: {cpp_out}")

            if clang_format_exe:
                fmt = subprocess.run(
                    [clang_format_exe, "-i", str(header_out), str(cpp_out)],
                    check=False,
                )
                if fmt.returncode != 0:
                    print(
                        f"[warn] clang-format failed for {name} "
                        f"(exit={fmt.returncode}); leaving generated files unformatted."
                    )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

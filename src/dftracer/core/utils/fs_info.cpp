#include <dftracer/core/common/logging.h>
#include <dftracer/core/utils/fs_info.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysmacros.h>
#include <sys/vfs.h>
#include <unistd.h>

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace dftracer {
namespace {

// statfs(2) f_type values (linux/magic.h plus the parallel file systems that
// are not in it). Only used to name the type; an unknown one is written as hex.
const char* fs_type_name(unsigned long magic) {
  switch (magic) {
    case 0x0BD00BD0:
      return "lustre";
    case 0x47504653:
      return "gpfs";
    case 0x19830326:
      return "beegfs";
    case 0x00C36400:
      return "ceph";
    case 0x6969:
      return "nfs";
    case 0xEF53:
      return "ext4";
    case 0x58465342:
      return "xfs";
    case 0x9123683E:
      return "btrfs";
    case 0x2FC12FC1:
      return "zfs";
    case 0x01021994:
      return "tmpfs";
    case 0x794C7630:
      return "overlayfs";
    case 0x73717368:
      return "squashfs";
    case 0x65735546:
      return "fuse";
    case 0x9FA0:
      return "proc";
    case 0x62656572:
      return "sysfs";
    case 0x6E667364:
      return "nfsd";
    case 0xFF534D42:
      return "cifs";
    default:
      return nullptr;
  }
}

constexpr unsigned long kLustreMagic = 0x0BD00BD0;

// The subset of liblustreapi's llapi_layout API used here, declared rather
// than included so Lustre is not a build dependency. These signatures have
// been stable since Lustre 2.7 (struct llapi_layout is opaque).
// lustreapi.h: LLAPI_LAYOUT_INVALID and the values just above it are
// sentinels (default / all OSTs / ...) rather than real counts or sizes.
constexpr uint64_t kLayoutInvalid = 0x1000000000000001ULL;
constexpr uint64_t kLayoutSentinelEnd = kLayoutInvalid + 16;
constexpr uint64_t kLustreEof = 0xFFFFFFFFFFFFFFFFULL;
constexpr uint32_t kCompUseFirst = 1;  // LLAPI_LAYOUT_COMP_USE_FIRST
constexpr uint32_t kCompUseNext = 3;   // LLAPI_LAYOUT_COMP_USE_NEXT
// LLAPI_LAYOUT_GET_EXPECTED: fill in inherited values, so a directory's
// layout is what a new file in it gets even when the directory sets none.
constexpr int kLayoutGetExpected = 1;

struct LustreApi {
  void* (*layout_get_by_path)(const char*, int) = nullptr;
  void (*layout_free)(void*) = nullptr;
  int (*stripe_count_get)(const void*, uint64_t*) = nullptr;
  int (*stripe_size_get)(const void*, uint64_t*) = nullptr;
  int (*ost_index_get)(const void*, uint64_t, uint64_t*) = nullptr;
  int (*pool_name_get)(const void*, char*, size_t) = nullptr;
  // Optional: absent from older releases, or only used for extra fields.
  int (*pattern_get)(const void*, uint64_t*) = nullptr;
  int (*comp_use)(void*, uint32_t) = nullptr;
  int (*comp_extent_get)(const void*, uint64_t*, uint64_t*) = nullptr;
  int (*mirror_count_get)(void*, uint16_t*) = nullptr;
  int (*getname)(const char*, char*, size_t) = nullptr;
  int (*fget_mdtidx)(int, int*) = nullptr;
  bool ok = false;

  LustreApi() {
    void* h = dlopen("liblustreapi.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (h == nullptr) h = dlopen("liblustreapi.so", RTLD_LAZY | RTLD_LOCAL);
    if (h == nullptr) {
      DFTRACER_LOG_DEBUG("fs_info: liblustreapi not available: %s", dlerror());
      return;
    }
    bind(h, "llapi_layout_get_by_path", layout_get_by_path);
    bind(h, "llapi_layout_free", layout_free);
    bind(h, "llapi_layout_stripe_count_get", stripe_count_get);
    bind(h, "llapi_layout_stripe_size_get", stripe_size_get);
    bind(h, "llapi_layout_ost_index_get", ost_index_get);
    bind(h, "llapi_layout_pool_name_get", pool_name_get);
    bind(h, "llapi_layout_pattern_get", pattern_get);
    bind(h, "llapi_layout_comp_use", comp_use);
    bind(h, "llapi_layout_comp_extent_get", comp_extent_get);
    bind(h, "llapi_layout_mirror_count_get", mirror_count_get);
    bind(h, "llapi_getname", getname);
    bind(h, "llapi_file_fget_mdtidx", fget_mdtidx);
    // liblustreapi logs its own failures to stderr; the traced app's stderr
    // is not ours to write to, and a failure here only means a missing field.
    void (*msg_set_level)(int) = nullptr;
    bind(h, "llapi_msg_set_level", msg_set_level);
    if (msg_set_level != nullptr) msg_set_level(0);  // LLAPI_MSG_OFF
    ok = layout_get_by_path && layout_free && stripe_count_get &&
         stripe_size_get && ost_index_get;
    // The handle is kept for the life of the process, like libc's.
  }

  template <typename F>
  static void bind(void* h, const char* name, F& fn) {
    fn = reinterpret_cast<F>(dlsym(h, name));
  }

  static LustreApi& get() {
    static LustreApi api;
    return api;
  }
};

// lustreapi.h LLAPI_LAYOUT_* patterns. "mdt" is Data-on-MDT: the extent lives
// on the MDT and has no OSTs.
const char* pattern_name(uint64_t pattern) {
  switch (pattern) {
    case 0:
      return "raid0";
    case 2:
      return "mdt";
    case 4:
      return "overstriping";
    default:
      return nullptr;
  }
}

// A layout value as JSON: the number, or -1 for a sentinel (not instantiated
// yet / file-system default / all OSTs).
int64_t layout_value(uint64_t v) {
  if (v >= kLayoutInvalid && v < kLayoutSentinelEnd) return -1;
  return static_cast<int64_t>(v);
}

void write_osts(std::ostringstream& out, const std::set<uint64_t>& osts) {
  out << "[";
  bool first = true;
  for (auto ost : osts) {
    if (!first) out << ",";
    out << ost;
    first = false;
  }
  out << "]";
}

// The current component's stripe settings and OSTs, as JSON fields (without
// braces). The component's OSTs are added to all_osts too.
void write_component(const LustreApi& api, void* layout,
                     std::ostringstream& out, std::set<uint64_t>& all_osts) {
  uint64_t count = kLayoutInvalid, size = kLayoutInvalid;
  api.stripe_count_get(layout, &count);
  api.stripe_size_get(layout, &size);
  uint64_t pattern = kLayoutInvalid;
  const char* pname = nullptr;
  if (api.pattern_get != nullptr && api.pattern_get(layout, &pattern) == 0)
    pname = pattern_name(pattern);
  if (pname != nullptr) out << "\"pattern\":\"" << pname << "\",";
  // A DoM extent stores no stripes, whatever stripe_count_get reports.
  if (pattern == 2) count = 0;
  out << "\"stripe_count\":" << layout_value(count)
      << ",\"stripe_size\":" << layout_value(size);
  // OSTs exist only once a component is instantiated (written to); until
  // then ost_index_get fails and the list is empty.
  std::set<uint64_t> osts;
  if (layout_value(count) > 0) {
    for (uint64_t i = 0; i < count; ++i) {
      uint64_t ost = kLayoutInvalid;
      if (api.ost_index_get(layout, i, &ost) != 0) break;
      if (layout_value(ost) < 0) break;
      osts.insert(ost);
    }
  }
  out << ",\"osts\":";
  write_osts(out, osts);
  all_osts.insert(osts.begin(), osts.end());
}

// The Lustre file-system name of the mount holding path ("" if unknown).
std::string lustre_fsname(const char* path) {
  const LustreApi& api = LustreApi::get();
  if (!api.ok || api.getname == nullptr) return "";
  // "<fsname>-<instance>", e.g. "lustre5-ffff8f2b1c0e8000".
  char name[128] = {0};
  if (api.getname(path, name, sizeof(name)) != 0) return "";
  char* dash = strrchr(name, '-');
  if (dash != nullptr) *dash = '\0';
  return name;
}

// The layout fields of path (without braces), or "" when they cannot be read.
// For a file, its own layout; for a directory (flags kLayoutGetExpected), the
// one new files created in it get.
std::string lustre_layout_fields(const char* path, int flags) {
  const LustreApi& api = LustreApi::get();
  if (!api.ok) return "";
  void* layout = api.layout_get_by_path(path, flags);
  if (layout == nullptr) {
    DFTRACER_LOG_DEBUG("fs_info: no Lustre layout for %s", path);
    return "";
  }
  std::ostringstream out;
  if (api.pool_name_get != nullptr) {
    char pool[64] = {0};
    if (api.pool_name_get(layout, pool, sizeof(pool)) == 0) {
      out << "\"pool\":\"" << pool << "\",";
    }
  }
  if (api.mirror_count_get != nullptr) {
    uint16_t mirrors = 0;
    if (api.mirror_count_get(layout, &mirrors) == 0) {
      out << "\"mirrors\":" << mirrors << ",";
    }
  }

  // A plain layout is one component, written at the top level. A composite
  // (PFL / FLR) one has a list, each covering an extent of the file, under
  // "components"; its top level has only the OSTs of all of them, so "which
  // OSTs does this file touch" needs no walking of the components.
  std::set<uint64_t> all_osts;
  std::ostringstream first, comps;
  int ncomps = 0;
  bool walk = api.comp_use != nullptr && api.comp_extent_get != nullptr &&
              api.comp_use(layout, kCompUseFirst) == 0;
  if (walk) {
    do {
      std::ostringstream comp;
      write_component(api, layout, comp, all_osts);
      uint64_t start = 0, end = kLustreEof;
      api.comp_extent_get(layout, &start, &end);
      if (ncomps == 0) first << comp.str();
      comps << (ncomps ? "," : "") << "{\"start\":" << start << ",\"end\":"
            << (end == kLustreEof ? -1 : static_cast<int64_t>(end)) << ","
            << comp.str() << "}";
      ++ncomps;
    } while (api.comp_use(layout, kCompUseNext) == 0);
  } else {
    write_component(api, layout, first, all_osts);
    ncomps = 1;
  }
  api.layout_free(layout);

  if (ncomps > 1) {
    out << "\"osts\":";
    write_osts(out, all_osts);
    out << ",\"components\":[" << comps.str() << "]";
  } else {
    out << first.str();
  }
  return out.str();
}

// The MDT a Lustre directory lives on, or -1.
int lustre_mdt(const char* dir) {
  const LustreApi& api = LustreApi::get();
  if (!api.ok || api.fget_mdtidx == nullptr) return -1;
  int fd = open(dir, O_RDONLY | O_DIRECTORY);
  if (fd < 0) return -1;
  int mdt = -1;
  if (api.fget_mdtidx(fd, &mdt) != 0) mdt = -1;
  close(fd);
  return mdt;
}

// A JSON string literal for s. The trace serializer writes strings as given,
// and mount sources/options/paths are arbitrary text.
std::string json_str(const std::string& s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += static_cast<char>(c);
    } else if (c < 0x20) {
      char esc[8];
      snprintf(esc, sizeof(esc), "\\u%04x", c);
      out += esc;
    } else {
      out += static_cast<char>(c);
    }
  }
  return out + "\"";
}

// One line of /proc/self/mountinfo (proc(5)): the fields used here.
struct MountEntry {
  unsigned major = 0, minor = 0;
  std::string point;    // mount point
  std::string type;     // e.g. "nfs", "lustre", "fuse.sshfs"
  std::string source;   // e.g. "server:/export", "/dev/nvme0n1p1"
  std::string options;  // per-mount options, then the super block's
};

// mountinfo escapes space, tab, newline and backslash as \ooo.
std::string mountinfo_unescape(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 3 < s.size() && s[i + 1] >= '0' &&
        s[i + 1] <= '3') {
      out += static_cast<char>(strtol(s.substr(i + 1, 3).c_str(), nullptr, 8));
      i += 3;
    } else {
      out += s[i];
    }
  }
  return out;
}

std::vector<MountEntry> read_mountinfo() {
  std::vector<MountEntry> mounts;
  std::ifstream in("/proc/self/mountinfo");
  std::string line;
  while (std::getline(in, line)) {
    // id parent major:minor root point options [optional...] - type source
    // super-options
    std::istringstream fields(line);
    std::string id, parent, devno, root, point, options, tok;
    if (!(fields >> id >> parent >> devno >> root >> point >> options))
      continue;
    while (fields >> tok && tok != "-") {
    }
    MountEntry m;
    std::string super_options;
    if (!(fields >> m.type >> m.source)) continue;
    fields >> super_options;
    if (sscanf(devno.c_str(), "%u:%u", &m.major, &m.minor) != 2) continue;
    m.point = mountinfo_unescape(point);
    m.source = mountinfo_unescape(m.source);
    m.options = options;
    if (!super_options.empty()) m.options += "," + super_options;
    mounts.push_back(std::move(m));
  }
  return mounts;
}

bool under(const std::string& path, const std::string& point) {
  return point == "/" ||
         (path.compare(0, point.size(), point) == 0 &&
          (path.size() == point.size() || path[point.size()] == '/'));
}

// The mount holding the resolved path real, whose device is dev: the longest
// mount point above it on that device (several mounts can share one: bind
// mounts, the same export twice), else the longest above it at all (btrfs
// subvolumes report a device of their own).
const MountEntry* find_mount(const std::vector<MountEntry>& mounts,
                             const std::string& real, dev_t dev) {
  const MountEntry* best = nullptr;
  const MountEntry* best_any = nullptr;
  for (const auto& m : mounts) {
    if (!under(real, m.point)) continue;
    if (best_any == nullptr || m.point.size() >= best_any->point.size())
      best_any = &m;
    if (m.major == major(dev) && m.minor == minor(dev) &&
        (best == nullptr || m.point.size() >= best->point.size()))
      best = &m;
  }
  return best != nullptr ? best : best_any;
}

// 1 for a spinning disk, 0 for flash, -1 when the file system is not on a
// local block device (network and pseudo file systems) or it is not known.
int rotational(dev_t dev) {
  if (major(dev) == 0) return -1;  // anonymous device: no block device
  char base[64];
  snprintf(base, sizeof(base), "/sys/dev/block/%u:%u", major(dev), minor(dev));
  // A partition has no queue/ of its own; its disk's is one level up.
  for (const char* rel : {"/queue/rotational", "/../queue/rotational"}) {
    std::ifstream in(std::string(base) + rel);
    int value;
    if (in >> value) return value;
  }
  return -1;
}

std::string parent_dir(const std::string& path) {
  auto slash = path.find_last_of('/');
  if (slash == std::string::npos) return ".";
  if (slash == 0) return "/";
  return path.substr(0, slash);
}

std::string resolve(const std::string& path) {
  char resolved[PATH_MAX];
  return realpath(path.c_str(), resolved) != nullptr ? resolved : path;
}

}  // namespace

struct FsRecordWriter::Impl {
  struct Dir {
    std::string dhash;
    bool lustre = false;
  };
  HashFn hash;
  EmitFn emit;
  std::vector<MountEntry> mounts = read_mountinfo();
  // mount point (or "dev:<major>:<minor>" outside any listed mount) -> mhash
  std::unordered_map<std::string, std::string> mount_hash;
  // directory, as traced and resolved -> its record
  std::unordered_map<std::string, Dir> dirs;
  size_t records = 0;

  void write(Metadata* fields) {
    emit(fields);
    ++records;
  }

  // The mount record, written the first time the mount is seen. Everything in
  // it is the same for every file on the mount, and statfs is a server round
  // trip on NFS (hundreds of us), so it is looked up once per mount.
  std::string mount(const std::string& real, const struct stat& st) {
    const MountEntry* m = find_mount(mounts, real, st.st_dev);
    std::string key = m != nullptr ? m->point
                                   : "dev:" + std::to_string(major(st.st_dev)) +
                                         ":" + std::to_string(minor(st.st_dev));
    auto it = mount_hash.find(key);
    if (it != mount_hash.end()) return it->second;
    std::string mhash = hash(key);
    mount_hash.emplace(key, mhash);

    struct statfs sf{};
    auto fields = new Metadata();
    fields->insert_or_assign("level", std::string("mount"));
    fields->insert_or_assign("mhash", mhash);
    if (statfs(real.c_str(), &sf) == 0) {
      auto magic = static_cast<unsigned long>(sf.f_type);
      const char* name = fs_type_name(magic);
      char hex[32];
      if (name == nullptr) {
        snprintf(hex, sizeof(hex), "0x%lx", magic);
        name = hex;
      }
      fields->insert_or_assign("fs", std::string(name));
      fields->insert_or_assign("bsize", static_cast<int64_t>(sf.f_bsize));
      uint64_t frsize = sf.f_frsize ? sf.f_frsize : sf.f_bsize;
      std::ostringstream cap;
      cap << "{\"total_bytes\":" << sf.f_blocks * frsize
          << ",\"free_bytes\":" << sf.f_bfree * frsize
          << ",\"avail_bytes\":" << sf.f_bavail * frsize
          << ",\"total_inodes\":" << sf.f_files
          << ",\"free_inodes\":" << sf.f_ffree << "}";
      fields->insert_or_assign("capacity", RawJson(cap.str()));
      fields->insert_or_assign("namelen", static_cast<int64_t>(sf.f_namelen));
      fields->insert_or_assign("read_only", (sf.f_flags & ST_RDONLY) ? 1 : 0);
      if (magic == kLustreMagic) {
        // Only now, with a file really on Lustre, is liblustreapi loaded: a
        // node without Lustre never tries. Where the mount is Lustre but the
        // library is not installed, say so, since the dir and file records
        // then have no layouts.
        if (!LustreApi::get().ok) {
          fields->insert_or_assign("lustre", RawJson("{\"liblustreapi\":0}"));
        } else {
          std::string fsname = lustre_fsname(real.c_str());
          fields->insert_or_assign(
              "lustre",
              RawJson("{\"liblustreapi\":1" +
                      (fsname.empty() ? std::string()
                                      : ",\"fsname\":" + json_str(fsname)) +
                      "}"));
        }
      }
    }
    int rot = rotational(st.st_dev);
    if (rot >= 0) fields->insert_or_assign("rotational", rot);
    if (m != nullptr) {
      fields->insert_or_assign(
          "mount", RawJson("{\"type\":" + json_str(m->type) +
                           ",\"source\":" + json_str(m->source) +
                           ",\"options\":" + json_str(m->options) + "}"));
    }
    write(fields);
    return mhash;
  }

  // The directory's record, written (after its mount's) the first time the
  // directory is seen. nullptr when it no longer exists.
  const Dir* dir(const std::string& path) {
    auto it = dirs.find(path);
    if (it != dirs.end()) return &it->second;
    // Two spellings of one directory (relative, through a symlink) share a
    // record: it is keyed and hashed by its resolved path.
    std::string real = resolve(path);
    it = dirs.find(real);
    if (it != dirs.end()) {
      Dir copy = it->second;
      return &dirs.emplace(path, copy).first->second;
    }
    struct stat st{};
    if (stat(real.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return nullptr;
    struct statfs sf{};
    Dir d;
    d.lustre = statfs(real.c_str(), &sf) == 0 &&
               static_cast<unsigned long>(sf.f_type) == kLustreMagic;
    std::string mhash = mount(real, st);
    d.dhash = hash(real);

    auto fields = new Metadata();
    fields->insert_or_assign("level", std::string("dir"));
    fields->insert_or_assign("dhash", d.dhash);
    fields->insert_or_assign("mhash", mhash);
    if (d.lustre) {
      std::ostringstream lustre;
      lustre << "{\"mdt\":" << lustre_mdt(real.c_str());
      std::string layout =
          lustre_layout_fields(real.c_str(), kLayoutGetExpected);
      if (!layout.empty()) lustre << "," << layout;
      lustre << "}";
      fields->insert_or_assign("lustre", RawJson(lustre.str()));
    }
    write(fields);
    dirs.emplace(real, d);
    return &dirs.emplace(path, d).first->second;
  }
};

FsRecordWriter::FsRecordWriter(HashFn hash, EmitFn emit) : impl_(new Impl()) {
  impl_->hash = std::move(hash);
  impl_->emit = std::move(emit);
}

FsRecordWriter::~FsRecordWriter() = default;

size_t FsRecordWriter::records() const { return impl_->records; }

bool FsRecordWriter::add(const std::string& path, const std::string& fhash) {
  struct stat st{};
  if (stat(path.c_str(), &st) != 0) return false;
  // A traced directory (e.g. the cwd) is described by its dir record alone.
  if (S_ISDIR(st.st_mode)) return impl_->dir(path) != nullptr;
  const Impl::Dir* d = impl_->dir(parent_dir(path));

  // Only what differs from file to file: everything about the directory and
  // the mount is in their records, joined through dhash -> mhash.
  auto fields = new Metadata();
  fields->insert_or_assign("level", std::string("file"));
  fields->insert_or_assign("fhash", fhash);
  if (d != nullptr) fields->insert_or_assign("dhash", d->dhash);
  fields->insert_or_assign("size", static_cast<int64_t>(st.st_size));
  // Preferred I/O size for this file; on Lustre it follows the stripe size
  // and count rather than the file system's bsize.
  fields->insert_or_assign("blksize", static_cast<int64_t>(st.st_blksize));
  if (d != nullptr && d->lustre) {
    std::string layout = lustre_layout_fields(path.c_str(), 0);
    if (!layout.empty())
      fields->insert_or_assign("lustre", RawJson("{" + layout + "}"));
  }
  impl_->write(fields);
  return true;
}

}  // namespace dftracer

#ifndef DFTRACER_UTILS_FS_INFO_H
#define DFTRACER_UTILS_FS_INFO_H

#include <dftracer/core/common/datastructure.h>
#include <sys/types.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace dftracer {

// File-system facts about the traced files, written as "FS" metadata records
// (METADATA_NAME_FILE_SYSTEM) at finalize. Each fact is stored once, at the
// level where it can change, and the levels are linked by hash:
//
//   "level":"mount"  one per mount, keyed by "mhash" (the mount point's FH
//                    hash): fs type, bsize, capacity, namelen, read_only,
//                    rotational (local block devices), the mountinfo
//                    type/source/options and, on Lustre, the fsname
//   "level":"dir"    one per directory, keyed by "dhash" (its FH hash), with
//                    the "mhash" it is on and, on Lustre, its MDT and the
//                    layout new files in it get
//   "level":"file"   one per traced file, keyed by "fhash", with the "dhash"
//                    it is in: size, blksize and, on Lustre, its own layout
//                    (stripes, pool, the OSTs it is on)
//
// liblustreapi is dlopen()ed on first use, so DFTracer neither needs Lustre at
// build time nor fails to load where there is no Lustre client: without the
// library the "lustre" objects are just left out.
class FsRecordWriter {
 public:
  // hash: the FH hash of a path, writing its FH record if it is new.
  // emit: writes one FS record and takes ownership of its fields.
  using HashFn = std::function<std::string(const std::string& path)>;
  using EmitFn = std::function<void(Metadata* fields)>;
  FsRecordWriter(HashFn hash, EmitFn emit);
  ~FsRecordWriter();

  // Writes the file's record, preceded by its directory's and mount's the
  // first time either is seen. A directory gets only its dir record. Returns
  // false when path no longer exists (removed before finalize).
  bool add(const std::string& path, const std::string& fhash);
  // FS records written so far, of all levels.
  size_t records() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace dftracer

#endif  // DFTRACER_UTILS_FS_INFO_H

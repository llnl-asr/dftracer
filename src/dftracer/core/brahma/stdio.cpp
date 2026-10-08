//
// Created by hariharan on 8/16/22.
//
#include <cpp-logger/logger.h>
#include <dftracer/core/brahma/stdio.h>
#include <dftracer/core/df_logger.h>
#include <dftracer/core/utils/stdio_bypass.h>

static ConstEventNameType CATEGORY = "STDIO";
static TraceEventType TRACE_TYPE = TraceEventType::TRACE_TYPE_LIBC_IO;

std::shared_ptr<brahma::STDIODFTracer> brahma::STDIODFTracer::instance =
    nullptr;
bool brahma::STDIODFTracer::stop_trace = false;

FILE* brahma::STDIODFTracer::fopen64(const char* path, const char* mode) {
  BRAHMA_MAP_OR_FAIL(fopen64);
  DFT_LOGGER_START(path);
  DFT_LOGGER_UPDATE_TYPE(mode, MetadataType::MT_VALUE);
  FILE* ret = __real_fopen64(path, mode);
  DFT_LOGGER_END();
  if (trace) this->trace(ret, fhash);
  return ret;
}

FILE* brahma::STDIODFTracer::fopen(const char* path, const char* mode) {
  BRAHMA_MAP_OR_FAIL(fopen);
  DFT_LOGGER_START(path);
  DFT_LOGGER_UPDATE_TYPE(mode, MetadataType::MT_VALUE);
  FILE* ret = __real_fopen(path, mode);
  DFT_LOGGER_END();
  if (trace) this->trace(ret, fhash);
  return ret;
}

int brahma::STDIODFTracer::fclose(FILE* fp) {
  BRAHMA_MAP_OR_FAIL(fclose);
  DFT_LOGGER_START(fp);
  int ret = __real_fclose(fp);
  DFT_LOGGER_END();
  if (trace) this->remove_trace(fp);
  return ret;
}

size_t brahma::STDIODFTracer::fread(void* ptr, size_t size, size_t count,
                                    FILE* fp) {
  BRAHMA_MAP_OR_FAIL(fread);
  DFT_LOGGER_START(fp);
  DFT_LOGGER_UPDATE_TYPE(size, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(count, MetadataType::MT_VALUE);
  size_t ret = __real_fread(ptr, size, count, fp);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

size_t brahma::STDIODFTracer::fwrite(const void* ptr, size_t size, size_t count,
                                     FILE* fp) {
  auto handle = fwrite_brahma_handle;
  (void)handle;
  BRAHMA_MAP_OR_FAIL(fwrite);
  DFT_LOGGER_START(fp);
  DFT_LOGGER_UPDATE_TYPE(size, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(count, MetadataType::MT_VALUE);
  size_t ret = __real_fwrite(ptr, size, count, fp);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

long brahma::STDIODFTracer::ftell(FILE* fp) {
  BRAHMA_MAP_OR_FAIL(ftell);
  DFT_LOGGER_START(fp);
  long ret = __real_ftell(fp);
  DFT_LOGGER_UPDATE(ret);
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::fseek(FILE* fp, long offset, int whence) {
  BRAHMA_MAP_OR_FAIL(fseek);
  DFT_LOGGER_START(fp);
  DFT_LOGGER_UPDATE_TYPE(offset, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(whence, MetadataType::MT_VALUE);
  int ret = __real_fseek(fp, offset, whence);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

void brahma::STDIODFTracer::clearerr(FILE* fp) {
  BRAHMA_MAP_OR_FAIL(clearerr);
  DFT_LOGGER_START(fp);
  __real_clearerr(fp);
  DFT_LOGGER_END();
}

int brahma::STDIODFTracer::feof(FILE* fp) {
  BRAHMA_MAP_OR_FAIL(feof);
  DFT_LOGGER_START(fp);
  int ret = __real_feof(fp);
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::ferror(FILE* fp) {
  BRAHMA_MAP_OR_FAIL(ferror);
  DFT_LOGGER_START(fp);
  int ret = __real_ferror(fp);
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::fgetc(FILE* fp) {
  BRAHMA_MAP_OR_FAIL(fgetc);
  DFT_LOGGER_START(fp);
  int ret = __real_fgetc(fp);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::fgetpos(FILE* fp, fpos_t* pos) {
  BRAHMA_MAP_OR_FAIL(fgetpos);
  DFT_LOGGER_START(fp);
  DFT_LOGGER_UPDATE_TYPE(pos, MetadataType::MT_VALUE);
  int ret = __real_fgetpos(fp, pos);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

char* brahma::STDIODFTracer::fgets(char* str, int num, FILE* fp) {
  BRAHMA_MAP_OR_FAIL(fgets);
  DFT_LOGGER_START(fp);
  DFT_LOGGER_UPDATE_TYPE(num, MetadataType::MT_VALUE);
  char* ret = __real_fgets(str, num, fp);
  if (ret != nullptr) {
    size_t ret_len = strlen(ret);
    DFT_LOGGER_UPDATE_TYPE(ret_len, MetadataType::MT_VALUE);
  }
  DFT_LOGGER_END();
  return ret;
}

// See the comment on these four overrides' declarations in stdio.h: unlike
// every other override in this class, they call
// dftracer::STDIOBypass::get_instance() for the "real function" step
// instead of BRAHMA_MAP_OR_FAIL/__real_*, to avoid GOTCHA's broken
// gotcha_get_wrappee() resolution for these specific symbols.
void brahma::STDIODFTracer::flockfile(FILE* fp) {
  DFT_LOGGER_START(fp);
  dftracer::STDIOBypass::get_instance().flockfile(fp);
  DFT_LOGGER_END();
}

void brahma::STDIODFTracer::funlockfile(FILE* fp) {
  DFT_LOGGER_START(fp);
  dftracer::STDIOBypass::get_instance().funlockfile(fp);
  DFT_LOGGER_END();
}

int brahma::STDIODFTracer::ftrylockfile(FILE* fp) {
  DFT_LOGGER_START(fp);
  int ret = dftracer::STDIOBypass::get_instance().ftrylockfile(fp);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::fflush(FILE* fp) {
  DFT_LOGGER_START(fp);
  int ret = dftracer::STDIOBypass::get_instance().fflush(fp);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::fputc(int c, FILE* fp) {
  BRAHMA_MAP_OR_FAIL(fputc);
  DFT_LOGGER_START(fp);
  int ret = __real_fputc(c, fp);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::fputs(const char* str, FILE* fp) {
  BRAHMA_MAP_OR_FAIL(fputs);
  DFT_LOGGER_START(fp);
  if (str != nullptr) {
    size_t str_len = strlen(str);
    DFT_LOGGER_UPDATE_TYPE(str_len, MetadataType::MT_VALUE);
  }
  int ret = __real_fputs(str, fp);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

FILE* brahma::STDIODFTracer::freopen(const char* path, const char* mode,
                                     FILE* fp) {
  BRAHMA_MAP_OR_FAIL(freopen);
  DFT_LOGGER_START(fp);
  DFT_LOGGER_UPDATE_HASH(path);
  DFT_LOGGER_UPDATE_TYPE(mode, MetadataType::MT_VALUE);
  FILE* ret = __real_freopen(path, mode, fp);
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::fsetpos(FILE* fp, const fpos_t* pos) {
  BRAHMA_MAP_OR_FAIL(fsetpos);
  DFT_LOGGER_START(fp);
  DFT_LOGGER_UPDATE_TYPE(pos, MetadataType::MT_VALUE);
  int ret = __real_fsetpos(fp, pos);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::getc(FILE* fp) {
  BRAHMA_MAP_OR_FAIL(getc);
  DFT_LOGGER_START(fp);
  int ret = __real_getc(fp);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::getc_unlocked(FILE* fp) {
  BRAHMA_MAP_OR_FAIL(getc_unlocked);
  DFT_LOGGER_START(fp);
  int ret = __real_getc_unlocked(fp);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::getw(FILE* fp) {
  BRAHMA_MAP_OR_FAIL(getw);
  DFT_LOGGER_START(fp);
  int ret = __real_getw(fp);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::pclose(FILE* fp) {
  BRAHMA_MAP_OR_FAIL(pclose);
  DFT_LOGGER_START(fp);
  int ret = __real_pclose(fp);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::putw(int w, FILE* fp) {
  BRAHMA_MAP_OR_FAIL(putw);
  DFT_LOGGER_START(fp);
  int ret = __real_putw(w, fp);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

void brahma::STDIODFTracer::rewind(FILE* fp) {
  BRAHMA_MAP_OR_FAIL(rewind);
  DFT_LOGGER_START(fp);
  __real_rewind(fp);
  DFT_LOGGER_END();
}

int brahma::STDIODFTracer::setvbuf(FILE* fp, char* buf, int mode, size_t size) {
  BRAHMA_MAP_OR_FAIL(setvbuf);
  DFT_LOGGER_START(fp);
  DFT_LOGGER_UPDATE_TYPE(mode, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(size, MetadataType::MT_VALUE);
  int ret = __real_setvbuf(fp, buf, mode, size);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::ungetc(int c, FILE* fp) {
  BRAHMA_MAP_OR_FAIL(ungetc);
  DFT_LOGGER_START(fp);
  int ret = __real_ungetc(c, fp);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

// --- New overrides mirroring brahma's expanded STDIO interception surface ---

FILE* brahma::STDIODFTracer::freopen64(const char* path, const char* mode,
                                       FILE* fp) {
  BRAHMA_MAP_OR_FAIL(freopen64);
  DFT_LOGGER_START(fp);
  DFT_LOGGER_UPDATE_HASH(path);
  DFT_LOGGER_UPDATE_TYPE(mode, MetadataType::MT_VALUE);
  FILE* ret = __real_freopen64(path, mode, fp);
  DFT_LOGGER_END();
  return ret;
}

// printf/scanf family: see the comment on these overrides' declarations in
// stdio.h -- the real-function step goes through dftracer::STDIOBypass
// instead of BRAHMA_MAP_OR_FAIL/__real_*.

int brahma::STDIODFTracer::vfprintf(FILE* stream, const char* format,
                                    va_list args) {
  DFT_LOGGER_START(stream);
  int ret =
      dftracer::STDIOBypass::get_instance().vfprintf(stream, format, args);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::fscanf(FILE* stream, const char* format,
                                  va_list args) {
  DFT_LOGGER_START(stream);
  int ret = dftracer::STDIOBypass::get_instance().vfscanf(stream, format, args);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::vfscanf(FILE* stream, const char* format,
                                   va_list args) {
  DFT_LOGGER_START(stream);
  int ret = dftracer::STDIOBypass::get_instance().vfscanf(stream, format, args);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::__isoc99_fscanf(FILE* stream, const char* format,
                                           va_list args) {
  DFT_LOGGER_START(stream);
  int ret = dftracer::STDIOBypass::get_instance().__isoc99_vfscanf(
      stream, format, args);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::__isoc99_vfscanf(FILE* stream, const char* format,
                                            va_list args) {
  DFT_LOGGER_START(stream);
  int ret = dftracer::STDIOBypass::get_instance().__isoc99_vfscanf(
      stream, format, args);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::__isoc23_fscanf(FILE* stream, const char* format,
                                           va_list args) {
  DFT_LOGGER_START(stream);
  int ret = dftracer::STDIOBypass::get_instance().__isoc23_vfscanf(
      stream, format, args);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::__isoc23_vfscanf(FILE* stream, const char* format,
                                            va_list args) {
  DFT_LOGGER_START(stream);
  int ret = dftracer::STDIOBypass::get_instance().__isoc23_vfscanf(
      stream, format, args);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::puts(const char* s) {
  BRAHMA_MAP_OR_FAIL(puts);
  DFT_LOGGER_START(stdout);
  if (s != nullptr) {
    size_t s_len = strlen(s);
    DFT_LOGGER_UPDATE_TYPE(s_len, MetadataType::MT_VALUE);
  }
  int ret = __real_puts(s);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::putchar(int c) {
  BRAHMA_MAP_OR_FAIL(putchar);
  DFT_LOGGER_START(stdout);
  int ret = __real_putchar(c);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::putc(int c, FILE* stream) {
  BRAHMA_MAP_OR_FAIL(putc);
  DFT_LOGGER_START(stream);
  int ret = __real_putc(c, stream);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::putc_unlocked(int c, FILE* stream) {
  BRAHMA_MAP_OR_FAIL(putc_unlocked);
  DFT_LOGGER_START(stream);
  int ret = __real_putc_unlocked(c, stream);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::getchar(void) {
  BRAHMA_MAP_OR_FAIL(getchar);
  DFT_LOGGER_START(stdin);
  int ret = __real_getchar();
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::getchar_unlocked(void) {
  BRAHMA_MAP_OR_FAIL(getchar_unlocked);
  DFT_LOGGER_START(stdin);
  int ret = __real_getchar_unlocked();
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

void brahma::STDIODFTracer::perror(const char* s) {
  BRAHMA_MAP_OR_FAIL(perror);
  DFT_LOGGER_START(stderr);
  __real_perror(s);
  DFT_LOGGER_END();
}

void brahma::STDIODFTracer::setbuf(FILE* stream, char* buf) {
  BRAHMA_MAP_OR_FAIL(setbuf);
  DFT_LOGGER_START(stream);
  __real_setbuf(stream, buf);
  DFT_LOGGER_END();
}

void brahma::STDIODFTracer::setbuffer(FILE* stream, char* buf, size_t size) {
  BRAHMA_MAP_OR_FAIL(setbuffer);
  DFT_LOGGER_START(stream);
  DFT_LOGGER_UPDATE_TYPE(size, MetadataType::MT_VALUE);
  __real_setbuffer(stream, buf, size);
  DFT_LOGGER_END();
}

void brahma::STDIODFTracer::setlinebuf(FILE* stream) {
  BRAHMA_MAP_OR_FAIL(setlinebuf);
  DFT_LOGGER_START(stream);
  __real_setlinebuf(stream);
  DFT_LOGGER_END();
}

ssize_t brahma::STDIODFTracer::getline(char** lineptr, size_t* n,
                                       FILE* stream) {
  BRAHMA_MAP_OR_FAIL(getline);
  DFT_LOGGER_START(stream);
  ssize_t ret = __real_getline(lineptr, n, stream);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

ssize_t brahma::STDIODFTracer::getdelim(char** lineptr, size_t* n, int delim,
                                        FILE* stream) {
  BRAHMA_MAP_OR_FAIL(getdelim);
  DFT_LOGGER_START(stream);
  DFT_LOGGER_UPDATE_TYPE(delim, MetadataType::MT_VALUE);
  ssize_t ret = __real_getdelim(lineptr, n, delim, stream);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

FILE* brahma::STDIODFTracer::fmemopen(void* buf, size_t size,
                                      const char* mode) {
  BRAHMA_MAP_OR_FAIL(fmemopen);
  DFT_LOGGER_START_ALWAYS();
  DFT_LOGGER_UPDATE_TYPE(size, MetadataType::MT_VALUE);
  FILE* ret = __real_fmemopen(buf, size, mode);
  DFT_LOGGER_END();
  return ret;
}

FILE* brahma::STDIODFTracer::open_memstream(char** ptr, size_t* sizeloc) {
  BRAHMA_MAP_OR_FAIL(open_memstream);
  DFT_LOGGER_START_ALWAYS();
  FILE* ret = __real_open_memstream(ptr, sizeloc);
  DFT_LOGGER_END();
  return ret;
}

FILE* brahma::STDIODFTracer::popen(const char* command, const char* type) {
  BRAHMA_MAP_OR_FAIL(popen);
  DFT_LOGGER_START(command);
  DFT_LOGGER_UPDATE_TYPE(type, MetadataType::MT_VALUE);
  FILE* ret = __real_popen(command, type);
  DFT_LOGGER_END();
  if (trace) this->trace(ret, fhash);
  return ret;
}

char* brahma::STDIODFTracer::tmpnam(char* s) {
  BRAHMA_MAP_OR_FAIL(tmpnam);
  DFT_LOGGER_START_ALWAYS();
  char* ret = __real_tmpnam(s);
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::fileno(FILE* stream) {
  BRAHMA_MAP_OR_FAIL(fileno);
  DFT_LOGGER_START(stream);
  int ret = __real_fileno(stream);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

FILE* brahma::STDIODFTracer::fdopen(int fd, const char* mode) {
  BRAHMA_MAP_OR_FAIL(fdopen);
  DFT_LOGGER_START(fd);
  DFT_LOGGER_UPDATE_TYPE(fd, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(mode, MetadataType::MT_VALUE);
  FILE* ret = __real_fdopen(fd, mode);
  DFT_LOGGER_END();
  if (trace && ret != nullptr) this->trace(ret, fhash);
  return ret;
}

FILE* brahma::STDIODFTracer::tmpfile(void) {
  BRAHMA_MAP_OR_FAIL(tmpfile);
  DFT_LOGGER_START_ALWAYS();
  FILE* ret = __real_tmpfile();
  DFT_LOGGER_END();
  return ret;
}

FILE* brahma::STDIODFTracer::tmpfile64(void) {
  BRAHMA_MAP_OR_FAIL(tmpfile64);
  DFT_LOGGER_START_ALWAYS();
  FILE* ret = __real_tmpfile64();
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::fseeko(FILE* stream, off_t offset, int whence) {
  BRAHMA_MAP_OR_FAIL(fseeko);
  DFT_LOGGER_START(stream);
  DFT_LOGGER_UPDATE_TYPE(offset, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(whence, MetadataType::MT_VALUE);
  int ret = __real_fseeko(stream, offset, whence);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::fseeko64(FILE* stream, off64_t offset, int whence) {
  BRAHMA_MAP_OR_FAIL(fseeko64);
  DFT_LOGGER_START(stream);
  DFT_LOGGER_UPDATE_TYPE(offset, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(whence, MetadataType::MT_VALUE);
  int ret = __real_fseeko64(stream, offset, whence);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

off_t brahma::STDIODFTracer::ftello(FILE* stream) {
  BRAHMA_MAP_OR_FAIL(ftello);
  DFT_LOGGER_START(stream);
  off_t ret = __real_ftello(stream);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

off64_t brahma::STDIODFTracer::ftello64(FILE* stream) {
  BRAHMA_MAP_OR_FAIL(ftello64);
  DFT_LOGGER_START(stream);
  off64_t ret = __real_ftello64(stream);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::fgetpos64(FILE* stream, fpos64_t* pos) {
  BRAHMA_MAP_OR_FAIL(fgetpos64);
  DFT_LOGGER_START(stream);
  int ret = __real_fgetpos64(stream, pos);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

int brahma::STDIODFTracer::fsetpos64(FILE* stream, const fpos64_t* pos) {
  BRAHMA_MAP_OR_FAIL(fsetpos64);
  DFT_LOGGER_START(stream);
  int ret = __real_fsetpos64(stream, pos);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

size_t brahma::STDIODFTracer::__fread_chk(void* ptr, size_t ptrlen, size_t size,
                                          size_t count, FILE* fp) {
  BRAHMA_MAP_OR_FAIL(__fread_chk);
  DFT_LOGGER_START(fp);
  DFT_LOGGER_UPDATE_TYPE(size, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(count, MetadataType::MT_VALUE);
  size_t ret = __real___fread_chk(ptr, ptrlen, size, count, fp);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

size_t brahma::STDIODFTracer::__fread_unlocked_chk(void* ptr, size_t ptrlen,
                                                   size_t size, size_t count,
                                                   FILE* fp) {
  BRAHMA_MAP_OR_FAIL(__fread_unlocked_chk);
  DFT_LOGGER_START(fp);
  DFT_LOGGER_UPDATE_TYPE(size, MetadataType::MT_VALUE);
  DFT_LOGGER_UPDATE_TYPE(count, MetadataType::MT_VALUE);
  size_t ret = __real___fread_unlocked_chk(ptr, ptrlen, size, count, fp);
  DFT_LOGGER_UPDATE_TYPE(ret, MetadataType::MT_VALUE);
  DFT_LOGGER_END();
  return ret;
}

char* brahma::STDIODFTracer::__fgets_chk(char* str, size_t buflen, int num,
                                         FILE* fp) {
  BRAHMA_MAP_OR_FAIL(__fgets_chk);
  DFT_LOGGER_START(fp);
  DFT_LOGGER_UPDATE_TYPE(num, MetadataType::MT_VALUE);
  char* ret = __real___fgets_chk(str, buflen, num, fp);
  DFT_LOGGER_END();
  return ret;
}

char* brahma::STDIODFTracer::__fgets_unlocked_chk(char* str, size_t buflen,
                                                  int num, FILE* fp) {
  BRAHMA_MAP_OR_FAIL(__fgets_unlocked_chk);
  DFT_LOGGER_START(fp);
  DFT_LOGGER_UPDATE_TYPE(num, MetadataType::MT_VALUE);
  char* ret = __real___fgets_unlocked_chk(str, buflen, num, fp);
  DFT_LOGGER_END();
  return ret;
}

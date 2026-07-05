//
// Created by hariharan on 8/16/22.
//
#include <cpp-logger/logger.h>
#include <dftracer/core/brahma/stdio.h>
#include <dftracer/core/df_logger.h>
#include <dftracer/core/utils/stdio_bypass.h>

static ConstEventNameType CATEGORY = "STDIO";

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

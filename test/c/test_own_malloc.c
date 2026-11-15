/* An application with its own malloc that calls sysconf when a thread first
 * allocates, as MVAPICH2's ptmalloc does when it makes the thread's arena.
 * dftracer allocates while it records that sysconf, before the arena is
 * ready, so it must not trace the sysconf its own allocation makes, or each
 * event starts another one until the stack overflows (or the thread blocks
 * on a lock it holds). */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ARENA_SIZE (256UL << 20)
#define MIN_ALIGN 16

static char arena[ARENA_SIZE] __attribute__((aligned(4096)));
static size_t arena_used;
static __thread int arena_ready;

static void *arena_alloc(size_t align, size_t size) {
  if (align < MIN_ALIGN) align = MIN_ALIGN;
  if ((align & (align - 1)) != 0) {
    errno = EINVAL;
    return NULL;
  }
  size_t need = size + align + sizeof(size_t);
  size_t off = __atomic_fetch_add(&arena_used, need, __ATOMIC_RELAXED);
  if (need < size || off + need > ARENA_SIZE) {
    errno = ENOMEM;
    return NULL;
  }
  uintptr_t base = (uintptr_t)(arena + off) + sizeof(size_t);
  uintptr_t p = (base + align - 1) & ~(uintptr_t)(align - 1);
  ((size_t *)p)[-1] = size;
  return (void *)p;
}

void *memalign(size_t align, size_t size) {
  if (!arena_ready) {
    (void)sysconf(_SC_PAGESIZE);
    arena_ready = 1;
  }
  return arena_alloc(align, size);
}
void *malloc(size_t size) { return memalign(MIN_ALIGN, size); }
void free(void *p) { (void)p; }
void *calloc(size_t n, size_t size) {
  if (size != 0 && n > SIZE_MAX / size) {
    errno = ENOMEM;
    return NULL;
  }
  void *p = malloc(n * size);
  if (p != NULL) memset(p, 0, n * size);
  return p;
}
void *realloc(void *old, size_t size) {
  void *p = malloc(size);
  if (p != NULL && old != NULL) {
    size_t old_size = ((size_t *)old)[-1];
    memcpy(p, old, old_size < size ? old_size : size);
  }
  return p;
}
int posix_memalign(void **out, size_t align, size_t size) {
  void *p = memalign(align, size);
  if (p == NULL) return errno;
  *out = p;
  return 0;
}
void *aligned_alloc(size_t align, size_t size) { return memalign(align, size); }
void *valloc(size_t size) { return memalign(4096, size); }
void *pvalloc(size_t size) { return memalign(4096, (size + 4095) & ~4095UL); }
size_t malloc_usable_size(void *p) { return p ? ((size_t *)p)[-1] : 0; }

static void *write_file(void *arg) {
  const char *dir = arg;
  char path[4096];
  /* Also called directly: under valgrind its own malloc replaces this one. */
  if (sysconf(_SC_NPROCESSORS_ONLN) < 1) return (void *)1;
  snprintf(path, sizeof path, "%s/own_malloc.dat", dir);
  char *buf = malloc(4096);
  if (buf == NULL) return (void *)1;
  memset(buf, 'x', 4096);
  void *failed = (void *)1;
  int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (fd >= 0) {
    if (write(fd, buf, 4096) == 4096) failed = NULL;
    close(fd);
    unlink(path);
  }
  free(buf);
  return failed;
}

int main(int argc, char **argv) {
  pthread_t thread;
  void *failed = (void *)1;
  if (pthread_create(&thread, NULL, write_file, argc > 1 ? argv[1] : ".") != 0)
    return 1;
  pthread_join(thread, &failed);
  return failed != NULL;
}

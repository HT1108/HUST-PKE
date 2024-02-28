#ifndef _SYNC_UTILS_H_
#define _SYNC_UTILS_H_

static inline void sync_barrier(volatile int *counter, int all) {

  int local;
  uint64 hartid = read_tp();
  
  asm volatile("amoadd.w %0, %2, (%1)\n"
               : "=r"(local)
               : "r"(counter), "r"(1)
               : "memory");

  if (local + 1 < all) {
    do {
      asm volatile("lw %0, (%1)\n" : "=r"(local) : "r"(counter) : "memory");
    } while (local < all);
  }
}

static inline int swap(volatile int* lock, int val) {
  int ret;
  asm volatile("amoswap.w %0, %1, (%2)\n"
    : "=r"(ret)
    : "r"(val), "r"(lock)
    : "memory"
    );
  return ret;
}

static inline void sync_lock(volatile int* lock) {
  while (swap(lock, 0) == 0);
}

static inline void sync_unlock(volatile int* lock) {
  if (swap(lock, 1) == 1) panic("unlock error");
}

#endif
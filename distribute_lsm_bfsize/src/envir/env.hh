#ifndef ENV_HH
#define ENV_HH

struct Env {
  Env(uint64_t bpk = 0) : bits_per_key(bpk) {}
  uint64_t bits_per_key;
};

#endif
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <elf.h>
#include <sys/mman.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <link.h>
#include <setjmp.h>
#include <pthread.h>
#include <ctype.h>
#include <dlfcn.h>
#include "pthread_impl.h"
#include "libc.h"
#include "dynlink.h"

#include "uthash.h"

#define DEPOFF 0x002bc5c0
#define DEPSZ 0x0023a2ca

static void error(const char *, ...);

#define MAXP2(a,b) (-(-(a)&-(b)))
#define ALIGN(x,y) ((x)+(y)-1 & -(y))

#define MHSZ 6  

#define BIT_MASK(bit)             (1 << (bit))
#define SET_BIT(value,bit)        ((value) |= BIT_MASK(bit))
#define SET_BIT_RANGE(value,l,r) ((value) |= (((1 << (l - 1)) - 1) ^ ((1 << (r)) - 1)))
#define CLEAR_BIT(value,bit)      ((value) &= ~BIT_MASK(bit))
#define TEST_BIT(value,bit)       (((value) & BIT_MASK(bit)) ? 1 : 0)

clock_t start_time;

char *must_have[] = {"remove_func", "remove_code_pages", "_init", "__funcs_on_exit", "_fini", "__stdio_exit", NULL};

struct debug {
  int ver;
  void *head;
  void (*bp)(void);
  int state;
  void *base;
};

struct td_index {
  size_t args[2];
  struct td_index *next;
};

typedef struct pageinfo {
  void *page_num; //key
  size_t load_fn_count;
  size_t fn_count;
  unsigned int page_map;
  UT_hash_handle hh;
} pageinfo_t;

typedef struct funcinfo {
  char *name; //key
  void *start_addr;
  char weak;
  UT_hash_handle hh;
} funcinfo_t;

typedef struct loadinfo {
  void *start_addr; //key
  char loaded;
  size_t func_sz; //in bytes
  char removed;
  char *global_name;
  pageinfo_t *pageinfo;
  UT_hash_handle hh;
} loadinfo_t;

typedef struct dep_entry {
  char *name;
  void *start_addr;
  size_t size;
} dep_entry_t;

typedef struct dep_lst {
  char *name;
  void *start_addr; //key
  size_t func_sz;
  size_t nfuncs;
  dep_entry_t **funcs; //Array of dep_entry_t
  UT_hash_handle hh; //maps function name to its dependency list
} dep_lst_t;

dep_lst_t *dep_graph; //maps function name to its dependency list

struct dso {
#if DL_FDPIC
  struct fdpic_loadmap *loadmap;
#else
  unsigned char *base;
#endif
  char *name;
  size_t *dynv;
  struct dso *next, *prev;

  Phdr *phdr;
  Shdr *shdr;
  int phnum;
  int shnum;
  size_t phentsize;
  size_t shentsize;
  int refcnt;
  Sym *syms; /* Symbol Table (.dynsym) */
  uint32_t *hashtab;
  uint32_t *ghashtab;
  int16_t *versym;
  char *strings; /* String table (DT_STRTAB) */
  unsigned char *map;
  size_t map_len;
  dev_t dev;
  ino_t ino;
  signed char global;
  char relocated;
  char constructed;
  char kernel_mapped;
  struct dso **deps, *needed_by;
  char *rpath_orig, *rpath;
  struct tls_module tls;
  size_t tls_id;
  size_t relro_start, relro_end;
  void **new_dtv;
  unsigned char *new_tls;
  volatile int new_dtv_idx, new_tls_idx;
  struct td_index *td_index;
  struct dso *fini_next;
  char *shortname;
#if DL_FDPIC
  unsigned char *base;
#else
  struct fdpic_loadmap *loadmap;
#endif
  struct funcdesc {
    void *addr;
    size_t *got;
  } *funcdescs;
  size_t nfuncs;

  funcinfo_t *func_lst; //maps function name to its decription object
  loadinfo_t *load_lst; //maps function addr to its description object
  pageinfo_t *code_page_lst; //maps code page number to #fn in that page

  int (*mprotect_stub) (void *, size_t, int);
  void * (*memset_stub) (void *, const void *, size_t);
  size_t ncode_ptrs;
  size_t* code_ptrs;
  void *remove_list[10];
  size_t *got;
  char buf[];
};

struct symdef {
  Sym *sym;
  struct dso *dso;
};

int __init_tp(void *);
void __init_libc(char **, char *);
void *__copy_tls(unsigned char *);

__attribute__((__visibility__("hidden")))
const char *__libc_get_version(void);

static struct builtin_tls {
  char c;
  struct pthread pt;
  void *space[16];
} builtin_tls[1];
#define MIN_TLS_ALIGN offsetof(struct builtin_tls, pt)

#define ADDEND_LIMIT 4096
static size_t *saved_addends, *apply_addends_to;

static struct dso ldso;
static struct dso *head, *tail, *fini_head;
static char *env_path, *sys_path;
static unsigned long long gencnt;
static int runtime;
static int ldd_mode;
static int ldso_fail;
static int noload;
static jmp_buf *rtld_fail;
static pthread_rwlock_t lock;
static struct debug debug;
static struct tls_module *tls_tail;
static size_t tls_cnt, tls_offset, tls_align = MIN_TLS_ALIGN;
static size_t static_tls_cnt;
static pthread_mutex_t init_fini_lock = { ._m_type = PTHREAD_MUTEX_RECURSIVE };
static struct fdpic_loadmap *app_loadmap;
static struct fdpic_dummy_loadmap app_dummy_loadmap;

struct debug *_dl_debug_addr = &debug;

__attribute__((__visibility__("hidden")))
void (*const __init_array_start)(void)=0, (*const __fini_array_start)(void)=0;

__attribute__((__visibility__("hidden")))
extern void (*const __init_array_end)(void), (*const __fini_array_end)(void);

weak_alias(__init_array_start, __init_array_end);
weak_alias(__fini_array_start, __fini_array_end);

static int dl_strcmp(const char *l, const char *r)
{
  for (; *l==*r && *l; l++, r++);
  return *(unsigned char *)l - *(unsigned char *)r;
}
#define strcmp(l,r) dl_strcmp(l,r)

/* Compute load address for a virtual address in a given dso. */
#if DL_FDPIC
static void *laddr(const struct dso *p, size_t v)
{
  size_t j=0;
  if (!p->loadmap) return p->base + v;
  for (j=0; v-p->loadmap->segs[j].p_vaddr >= p->loadmap->segs[j].p_memsz; j++);
  return (void *)(v - p->loadmap->segs[j].p_vaddr + p->loadmap->segs[j].addr);
}
#define fpaddr(p, v) ((void (*)())&(struct funcdesc){ \
  laddr(p, v), (p)->got })
#else
#define laddr(p, v) (void *)((p)->base + (v))
#define fpaddr(p, v) ((void (*)())laddr(p, v))
#endif

static void decode_vec(size_t *v, size_t *a, size_t cnt)
{
  size_t i;
  for (i=0; i<cnt; i++) a[i] = 0;
  for (; v[0]; v+=2) if (v[0]-1<cnt-1) {
    a[0] |= 1UL<<v[0];
    a[v[0]] = v[1];
  }
}

static int search_vec(size_t *v, size_t *r, size_t key)
{
  for (; v[0]!=key; v+=2)
    if (!v[0]) return 0;
  *r = v[1];
  return 1;
}

static uint32_t sysv_hash(const char *s0)
{
  const unsigned char *s = (void *)s0;
  uint_fast32_t h = 0;
  while (*s) {
    h = 16*h + *s++;
    h ^= h>>24 & 0xf0;
  }
  return h & 0xfffffff;
}

static uint32_t gnu_hash(const char *s0)
{
  const unsigned char *s = (void *)s0;
  uint_fast32_t h = 5381;
  for (; *s; s++)
    h += h*32 + *s;
  return h;
}

static Sym *sysv_lookup(const char *s, uint32_t h, struct dso *dso)
{
  size_t i;
  Sym *syms = dso->syms;
  uint32_t *hashtab = dso->hashtab;
  char *strings = dso->strings;
  for (i=hashtab[2+h%hashtab[0]]; i; i=hashtab[2+hashtab[0]+i]) {
    if ((!dso->versym || dso->versym[i] >= 0)
        && (!strcmp(s, strings+syms[i].st_name)))
      return syms+i;
  }
  return 0;
}

static Sym *gnu_lookup(uint32_t h1, uint32_t *hashtab, struct dso *dso, const char *s)
{
  uint32_t nbuckets = hashtab[0];
  uint32_t *buckets = hashtab + 4 + hashtab[2]*(sizeof(size_t)/4);
  uint32_t i = buckets[h1 % nbuckets];

  if (!i) return 0;

  uint32_t *hashval = buckets + nbuckets + (i - hashtab[1]);

  for (h1 |= 1; ; i++) {
    uint32_t h2 = *hashval++;
    if ((h1 == (h2|1)) && (!dso->versym || dso->versym[i] >= 0)
        && !strcmp(s, dso->strings + dso->syms[i].st_name))
      return dso->syms+i;
    if (h2 & 1) break;
  }

  return 0;
}

static Sym *gnu_lookup_filtered(uint32_t h1, uint32_t *hashtab, struct dso *dso, const char *s, uint32_t fofs, size_t fmask)
{
  const size_t *bloomwords = (const void *)(hashtab+4);
  size_t f = bloomwords[fofs & (hashtab[2]-1)];
  if (!(f & fmask)) return 0;

  f >>= (h1 >> hashtab[3]) % (8 * sizeof f);
  if (!(f & 1)) return 0;

  return gnu_lookup(h1, hashtab, dso, s);
}

#define OK_TYPES (1<<STT_NOTYPE | 1<<STT_OBJECT | 1<<STT_FUNC | 1<<STT_COMMON | 1<<STT_TLS)
#define OK_BINDS (1<<STB_GLOBAL | 1<<STB_WEAK | 1<<STB_GNU_UNIQUE)

#ifndef ARCH_SYM_REJECT_UND
#define ARCH_SYM_REJECT_UND(s) 0
#endif

static struct symdef find_sym(struct dso *dso, const char *s, int need_def)
{
  uint32_t h = 0, gh, gho, *ght;
  size_t ghm = 0;
  struct symdef def = {0};
  for (; dso; dso=dso->next) {
    Sym *sym;
    if (!dso->global) continue;
    if ((ght = dso->ghashtab)) {
      if (!ghm) {
        gh = gnu_hash(s);
        int maskbits = 8 * sizeof ghm;
        gho = gh / maskbits;
        ghm = 1ul << gh % maskbits;
      }
      sym = gnu_lookup_filtered(gh, ght, dso, s, gho, ghm);
    } else {
      if (!h) h = sysv_hash(s);
      sym = sysv_lookup(s, h, dso);
    }
    if (!sym) continue;
    if (!sym->st_shndx)
      if (need_def || (sym->st_info&0xf) == STT_TLS
          || ARCH_SYM_REJECT_UND(sym))
        continue;
    if (!sym->st_value)
      if ((sym->st_info&0xf) != STT_TLS)
        continue;
    if (!(1<<(sym->st_info&0xf) & OK_TYPES)) continue;
    if (!(1<<(sym->st_info>>4) & OK_BINDS)) continue;

    if (def.sym && sym->st_info>>4 == STB_WEAK) continue;
    def.sym = sym;
    def.dso = dso;
    if (sym->st_info>>4 == STB_GLOBAL) break;
  }
  return def;
}

__attribute__((__visibility__("hidden")))
ptrdiff_t __tlsdesc_static(), __tlsdesc_dynamic();

//Update pageinfo when load new function
static void update_page_info(loadinfo_t *load) {
  void *page_num_start = (size_t)load->start_addr & -PAGE_SIZE;
  void *page_num_end = ((size_t) load->start_addr + load->func_sz) & -PAGE_SIZE;
  pageinfo_t *code_page = NULL;
  size_t page_num = (size_t) page_num_start;

  for(; page_num <= page_num_end; page_num+=PAGE_SIZE) {
    HASH_FIND_PTR(ldso.code_page_lst, &page_num, code_page);
    if(code_page) {
      code_page->load_fn_count++;
    }
  }
}

static void load_dep_funcs(struct dso *p, void *addr) {
  //Find dep funcs in dep_graph
  dep_lst_t *list = NULL;
  funcinfo_t *func = NULL;
  loadinfo_t *load = NULL;
  int i;
  
  HASH_FIND_PTR(dep_graph, &addr, list);
  if(list != NULL) {
    //Load all funcs in the list
    func = NULL;
    for(i=0; i<list->nfuncs; i++) {
      HASH_FIND_PTR(p->load_lst, &list->funcs[i]->start_addr, load);
      if(load){
      	load->loaded = 1;
        //dprintf(2, "Load %s %lx\n", load->global_name, load->start_addr);
        update_page_info(load);
      }else {
      	dprintf(2, "ERROR: Unable to load function %p\n", addr);
      }
    }
  }
}

static void do_relocs(struct dso *dso, size_t *rel, size_t rel_size, size_t stride)
{
  unsigned char *base = dso->base;
  Sym *syms = dso->syms;
  char *strings = dso->strings;
  Sym *sym;
  const char *name;
  void *ctx;
  int type;
  int sym_index;
  struct symdef def;
  size_t *reloc_addr;
  size_t sym_val;
  size_t tls_val;
  size_t addend;
  int skip_relative = 0, reuse_addends = 0, save_slot = 0;

  if (dso == &ldso) {
    /* Only ldso's REL table needs addend saving/reuse. */
    if (rel == apply_addends_to)
      reuse_addends = 1;
    skip_relative = 1;
  }

  for (; rel_size; rel+=stride, rel_size-=stride*sizeof(size_t)) {
    if (skip_relative && IS_RELATIVE(rel[1], dso->syms)) continue;
    type = R_TYPE(rel[1]);
    if (type == REL_NONE) continue;
    sym_index = R_SYM(rel[1]);
    reloc_addr = laddr(dso, rel[0]);
    if (sym_index) {
      sym = syms + sym_index;
      name = strings + sym->st_name;
      ctx = type==REL_COPY ? head->next : head;
      def = (sym->st_info&0xf) == STT_SECTION
        ? (struct symdef){ .dso = dso, .sym = sym }
        : find_sym(ctx, name, type==REL_PLT);
      if (!def.sym && (sym->st_shndx != SHN_UNDEF
          || sym->st_info>>4 != STB_WEAK)) {
        error("Error relocating %s: %s: symbol not found",
          dso->name, name);
        if (runtime) longjmp(*rtld_fail, 1);
        continue;
      }
    } else {
      sym = 0;
      def.sym = 0;
      def.dso = dso;
    }

    if (stride > 2) {
      addend = rel[2];
    } else if (type==REL_GOT || type==REL_PLT|| type==REL_COPY) {
      addend = 0;
    } else if (reuse_addends) {
      /* Save original addend in stage 2 where the dso
       * chain consists of just ldso; otherwise read back
       * saved addend since the inline one was clobbered. */
      if (head==&ldso)
        saved_addends[save_slot] = *reloc_addr;
      addend = saved_addends[save_slot++];
    } else {
      addend = *reloc_addr;
    }

    sym_val = def.sym ? (size_t)laddr(def.dso, def.sym->st_value) : 0;
    tls_val = def.sym ? def.sym->st_value : 0;

    switch(type) {
    case REL_NONE:
      break;
    case REL_OFFSET:
      addend -= (size_t)reloc_addr;
    case REL_SYMBOLIC:
    case REL_GOT:
    case REL_PLT:
      *reloc_addr = sym_val + addend;
      if(dso != &ldso) {
	      void * patch_addr = sym_val + addend;
	      loadinfo_t *load = NULL;
        funcinfo_t *func = NULL;
	      HASH_FIND_PTR(ldso.load_lst, &patch_addr, load);
	      if(load) {
	        load->loaded = 1;
          //dprintf(2, "load_func %s %p at %p\n", load->global_name, load->start_addr, reloc_addr);
          update_page_info(load);
	      }
	      load_dep_funcs(&ldso, patch_addr);
      }
      break;
    case REL_RELATIVE:
      *reloc_addr = (size_t)base + addend;
      break;
    case REL_SYM_OR_REL:
      if (sym) *reloc_addr = sym_val + addend;
      else *reloc_addr = (size_t)base + addend;
      break;
    case REL_COPY:
      memcpy(reloc_addr, (void *)sym_val, sym->st_size);
      break;
    case REL_OFFSET32:
      *(uint32_t *)reloc_addr = sym_val + addend
        - (size_t)reloc_addr;
      break;
    case REL_FUNCDESC:
      *reloc_addr = def.sym ? (size_t)(def.dso->funcdescs
        + (def.sym - def.dso->syms)) : 0;
      break;
    case REL_FUNCDESC_VAL:
      if ((sym->st_info&0xf) == STT_SECTION) *reloc_addr += sym_val;
      else *reloc_addr = sym_val;
      reloc_addr[1] = def.sym ? (size_t)def.dso->got : 0;
      break;
    case REL_DTPMOD:
      *reloc_addr = def.dso->tls_id;
      break;
    case REL_DTPOFF:
      *reloc_addr = tls_val + addend - DTP_OFFSET;
      break;
#ifdef TLS_ABOVE_TP
    case REL_TPOFF:
      *reloc_addr = tls_val + def.dso->tls.offset + TPOFF_K + addend;
      break;
#else
    case REL_TPOFF:
      *reloc_addr = tls_val - def.dso->tls.offset + addend;
      break;
    case REL_TPOFF_NEG:
      *reloc_addr = def.dso->tls.offset - tls_val + addend;
      break;
#endif
    case REL_TLSDESC:
      if (stride<3) addend = reloc_addr[1];
      if (runtime && def.dso->tls_id >= static_tls_cnt) {
        struct td_index *new = malloc(sizeof *new);
        if (!new) {
          error(
          "Error relocating %s: cannot allocate TLSDESC for %s",
          dso->name, sym ? name : "(local)" );
          longjmp(*rtld_fail, 1);
        }
        new->next = dso->td_index;
        dso->td_index = new;
        new->args[0] = def.dso->tls_id;
        new->args[1] = tls_val + addend;
        reloc_addr[0] = (size_t)__tlsdesc_dynamic;
        reloc_addr[1] = (size_t)new;
      } else {
        reloc_addr[0] = (size_t)__tlsdesc_static;
#ifdef TLS_ABOVE_TP
        reloc_addr[1] = tls_val + def.dso->tls.offset
          + TPOFF_K + addend;
#else
        reloc_addr[1] = tls_val - def.dso->tls.offset
          + addend;
#endif
      }
      break;
    default:
      error("Error relocating %s: unsupported relocation type %d",
        dso->name, type);
      if (runtime) longjmp(*rtld_fail, 1);
      continue;
    }
  }
}

/* A huge hack: to make up for the wastefulness of shared libraries
 * needing at least a page of dirty memory even if they have no global
 * data, we reclaim the gaps at the beginning and end of writable maps
 * and "donate" them to the heap by setting up minimal malloc
 * structures and then freeing them. */

static void reclaim(struct dso *dso, size_t start, size_t end)
{
  size_t *a, *z;
  if (start >= dso->relro_start && start < dso->relro_end) start = dso->relro_end;
  if (end   >= dso->relro_start && end   < dso->relro_end) end = dso->relro_start;
  start = start + 6*sizeof(size_t)-1 & -4*sizeof(size_t);
  end = (end & -4*sizeof(size_t)) - 2*sizeof(size_t);
  if (start>end || end-start < 4*sizeof(size_t)) return;
  a = laddr(dso, start);
  z = laddr(dso, end);
  a[-2] = 1;
  a[-1] = z[0] = end-start + 2*sizeof(size_t) | 1;
  z[1] = 1;
  free(a);
}

static void reclaim_gaps(struct dso *dso)
{
  Phdr *ph = dso->phdr;
  size_t phcnt = dso->phnum;

  if (DL_FDPIC) return; // FIXME
  for (; phcnt--; ph=(void *)((char *)ph+dso->phentsize)) {
    if (ph->p_type!=PT_LOAD) continue;
    if ((ph->p_flags&(PF_R|PF_W))!=(PF_R|PF_W)) continue;
    reclaim(dso, ph->p_vaddr & -PAGE_SIZE, ph->p_vaddr);
    reclaim(dso, ph->p_vaddr+ph->p_memsz,
      ph->p_vaddr+ph->p_memsz+PAGE_SIZE-1 & -PAGE_SIZE);
  }
}

static void *mmap_fixed(void *p, size_t n, int prot, int flags, int fd, off_t off)
{
  static int no_map_fixed;
  char *q;
  if (!no_map_fixed) {
    q = mmap(p, n, prot, flags|MAP_FIXED, fd, off);
    if (!DL_NOMMU_SUPPORT || q != MAP_FAILED || errno != EINVAL)
      return q;
    no_map_fixed = 1;
  }
  /* Fallbacks for MAP_FIXED failure on NOMMU kernels. */
  if (flags & MAP_ANONYMOUS) {
    memset(p, 0, n);
    return p;
  }
  ssize_t r;
  if (lseek(fd, off, SEEK_SET) < 0) return MAP_FAILED;
  for (q=p; n; q+=r, off+=r, n-=r) {
    r = read(fd, q, n);
    if (r < 0 && errno != EINTR) return MAP_FAILED;
    if (!r) {
      memset(q, 0, n);
      break;
    }
  }
  return p;
}

static void unmap_library(struct dso *dso)
{
  if (dso->loadmap) {
    size_t i;
    for (i=0; i<dso->loadmap->nsegs; i++) {
      if (!dso->loadmap->segs[i].p_memsz)
        continue;
      munmap((void *)dso->loadmap->segs[i].addr,
        dso->loadmap->segs[i].p_memsz);
    }
    free(dso->loadmap);
  } else if (dso->map && dso->map_len) {
    munmap(dso->map, dso->map_len);
  }
}

static void *map_library(int fd, struct dso *dso)
{
  Ehdr buf[(896+sizeof(Ehdr))/sizeof(Ehdr)];
  void *allocated_buf=0;
  size_t phsize;
  size_t addr_min=SIZE_MAX, addr_max=0, map_len;
  size_t this_min, this_max;
  size_t nsegs = 0;
  off_t off_start;
  Ehdr *eh;
  Phdr *ph, *ph0;
  unsigned prot;
  unsigned char *map=MAP_FAILED, *base;
  size_t dyn=0;
  size_t tls_image=0;
  size_t i;

  ssize_t l = read(fd, buf, sizeof buf);
  eh = buf;
  if (l<0) return 0;
  if (l<sizeof *eh || (eh->e_type != ET_DYN && eh->e_type != ET_EXEC))
    goto noexec;
  phsize = eh->e_phentsize * eh->e_phnum;
  if (phsize > sizeof buf - sizeof *eh) {
    allocated_buf = malloc(phsize);
    if (!allocated_buf) return 0;
    l = pread(fd, allocated_buf, phsize, eh->e_phoff);
    if (l < 0) goto error;
    if (l != phsize) goto noexec;
    ph = ph0 = allocated_buf;
  } else if (eh->e_phoff + phsize > l) {
    l = pread(fd, buf+1, phsize, eh->e_phoff);
    if (l < 0) goto error;
    if (l != phsize) goto noexec;
    ph = ph0 = (void *)(buf + 1);
  } else {
    ph = ph0 = (void *)((char *)buf + eh->e_phoff);
  }
  for (i=eh->e_phnum; i; i--, ph=(void *)((char *)ph+eh->e_phentsize)) {
    if (ph->p_type == PT_DYNAMIC) {
      dyn = ph->p_vaddr;
    } else if (ph->p_type == PT_TLS) {
      tls_image = ph->p_vaddr;
      dso->tls.align = ph->p_align;
      dso->tls.len = ph->p_filesz;
      dso->tls.size = ph->p_memsz;
    } else if (ph->p_type == PT_GNU_RELRO) {
      dso->relro_start = ph->p_vaddr & -PAGE_SIZE;
      dso->relro_end = (ph->p_vaddr + ph->p_memsz) & -PAGE_SIZE;
    }
    if (ph->p_type != PT_LOAD) continue;
    nsegs++;
    if (ph->p_vaddr < addr_min) {
      addr_min = ph->p_vaddr;
      off_start = ph->p_offset;
      prot = (((ph->p_flags&PF_R) ? PROT_READ : 0) |
        ((ph->p_flags&PF_W) ? PROT_WRITE: 0) |
        ((ph->p_flags&PF_X) ? PROT_EXEC : 0));
    }
    if (ph->p_vaddr+ph->p_memsz > addr_max) {
      addr_max = ph->p_vaddr+ph->p_memsz;
    }
  }
  if (!dyn) goto noexec;
  if (DL_FDPIC && !(eh->e_flags & FDPIC_CONSTDISP_FLAG)) {
    dso->loadmap = calloc(1, sizeof *dso->loadmap
      + nsegs * sizeof *dso->loadmap->segs);
    if (!dso->loadmap) goto error;
    dso->loadmap->nsegs = nsegs;
    for (ph=ph0, i=0; i<nsegs; ph=(void *)((char *)ph+eh->e_phentsize)) {
      if (ph->p_type != PT_LOAD) continue;
      prot = (((ph->p_flags&PF_R) ? PROT_READ : 0) |
        ((ph->p_flags&PF_W) ? PROT_WRITE: 0) |
        ((ph->p_flags&PF_X) ? PROT_EXEC : 0));
      map = mmap(0, ph->p_memsz + (ph->p_vaddr & PAGE_SIZE-1),
        prot, MAP_PRIVATE,
        fd, ph->p_offset & -PAGE_SIZE);
      if (map == MAP_FAILED) {
        unmap_library(dso);
        goto error;
      }
      dso->loadmap->segs[i].addr = (size_t)map +
        (ph->p_vaddr & PAGE_SIZE-1);
      dso->loadmap->segs[i].p_vaddr = ph->p_vaddr;
      dso->loadmap->segs[i].p_memsz = ph->p_memsz;
      i++;
      if (prot & PROT_WRITE) {
        size_t brk = (ph->p_vaddr & PAGE_SIZE-1)
          + ph->p_filesz;
        size_t pgbrk = brk + PAGE_SIZE-1 & -PAGE_SIZE;
        size_t pgend = brk + ph->p_memsz - ph->p_filesz
          + PAGE_SIZE-1 & -PAGE_SIZE;
        if (pgend > pgbrk && mmap_fixed(map+pgbrk,
          pgend-pgbrk, prot,
          MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS,
          -1, off_start) == MAP_FAILED)
          goto error;
        memset(map + brk, 0, pgbrk-brk);
      }
    }
    map = (void *)dso->loadmap->segs[0].addr;
    map_len = 0;
    goto done_mapping;
  }
  addr_max += PAGE_SIZE-1;
  addr_max &= -PAGE_SIZE;
  addr_min &= -PAGE_SIZE;
  off_start &= -PAGE_SIZE;
  map_len = addr_max - addr_min + off_start;
  /* The first time, we map too much, possibly even more than
   * the length of the file. This is okay because we will not
   * use the invalid part; we just need to reserve the right
   * amount of virtual address space to map over later. */
  map = DL_NOMMU_SUPPORT
    ? mmap((void *)addr_min, map_len, PROT_READ|PROT_WRITE|PROT_EXEC,
      MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)
    : mmap((void *)addr_min, map_len, prot,
      MAP_PRIVATE, fd, off_start);
  if (map==MAP_FAILED) goto error;
  dso->map = map;
  dso->map_len = map_len;
  /* If the loaded file is not relocatable and the requested address is
   * not available, then the load operation must fail. */
  if (eh->e_type != ET_DYN && addr_min && map!=(void *)addr_min) {
    errno = EBUSY;
    goto error;
  }
  base = map - addr_min;
  dso->phdr = 0;
  dso->phnum = 0;
  for (ph=ph0, i=eh->e_phnum; i; i--, ph=(void *)((char *)ph+eh->e_phentsize)) {
    if (ph->p_type != PT_LOAD) continue;
    /* Check if the programs headers are in this load segment, and
     * if so, record the address for use by dl_iterate_phdr. */
    if (!dso->phdr && eh->e_phoff >= ph->p_offset
        && eh->e_phoff+phsize <= ph->p_offset+ph->p_filesz) {
      dso->phdr = (void *)(base + ph->p_vaddr
        + (eh->e_phoff-ph->p_offset));
      dso->phnum = eh->e_phnum;
      dso->phentsize = eh->e_phentsize;
    }
    /* Reuse the existing mapping for the lowest-address LOAD */
    if ((ph->p_vaddr & -PAGE_SIZE) == addr_min && !DL_NOMMU_SUPPORT)
      continue;
    this_min = ph->p_vaddr & -PAGE_SIZE;
    this_max = ph->p_vaddr+ph->p_memsz+PAGE_SIZE-1 & -PAGE_SIZE;
    off_start = ph->p_offset & -PAGE_SIZE;
    prot = (((ph->p_flags&PF_R) ? PROT_READ : 0) |
      ((ph->p_flags&PF_W) ? PROT_WRITE: 0) |
      ((ph->p_flags&PF_X) ? PROT_EXEC : 0));
    if (mmap_fixed(base+this_min, this_max-this_min, prot, MAP_PRIVATE|MAP_FIXED, fd, off_start) == MAP_FAILED)
      goto error;
    if (ph->p_memsz > ph->p_filesz) {
      size_t brk = (size_t)base+ph->p_vaddr+ph->p_filesz;
      size_t pgbrk = brk+PAGE_SIZE-1 & -PAGE_SIZE;
      memset((void *)brk, 0, pgbrk-brk & PAGE_SIZE-1);
      if (pgbrk-(size_t)base < this_max && mmap_fixed((void *)pgbrk, (size_t)base+this_max-pgbrk, prot, MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS, -1, 0) == MAP_FAILED)
        goto error;
    }
  }
  for (i=0; ((size_t *)(base+dyn))[i]; i+=2)
    if (((size_t *)(base+dyn))[i]==DT_TEXTREL) {
      if (mprotect(map, map_len, PROT_READ|PROT_WRITE|PROT_EXEC)
          && errno != ENOSYS)
        goto error;
      break;
    }
done_mapping:
  dso->base = base;
  dso->dynv = laddr(dso, dyn);
  if (dso->tls.size) dso->tls.image = laddr(dso, tls_image);
  if (!runtime) reclaim_gaps(dso);
  free(allocated_buf);
  return map;
noexec:
  errno = ENOEXEC;
error:
  if (map!=MAP_FAILED) unmap_library(dso);
  free(allocated_buf);
  return 0;
}

static int path_open(const char *name, const char *s, char *buf, size_t buf_size)
{
  size_t l;
  int fd;
  for (;;) {
    s += strspn(s, ":\n");
    l = strcspn(s, ":\n");
    if (l-1 >= INT_MAX) return -1;
    if (snprintf(buf, buf_size, "%.*s/%s", (int)l, s, name) < buf_size) {
      if ((fd = open(buf, O_RDONLY|O_CLOEXEC))>=0) return fd;
      switch (errno) {
      case ENOENT:
      case ENOTDIR:
      case EACCES:
      case ENAMETOOLONG:
        break;
      default:
        /* Any negative value but -1 will inhibit
         * futher path search. */
        return -2;
      }
    }
    s += l;
  }
}

static int fixup_rpath(struct dso *p, char *buf, size_t buf_size)
{
  size_t n, l;
  const char *s, *t, *origin;
  char *d;
  if (p->rpath || !p->rpath_orig) return 0;
  if (!strchr(p->rpath_orig, '$')) {
    p->rpath = p->rpath_orig;
    return 0;
  }
  n = 0;
  s = p->rpath_orig;
  while ((t=strchr(s, '$'))) {
    if (strncmp(t, "$ORIGIN", 7) && strncmp(t, "${ORIGIN}", 9))
      return 0;
    s = t+1;
    n++;
  }
  if (n > SSIZE_MAX/PATH_MAX) return 0;

  if (p->kernel_mapped) {
    /* $ORIGIN searches cannot be performed for the main program
     * when it is suid/sgid/AT_SECURE. This is because the
     * pathname is under the control of the caller of execve.
     * For libraries, however, $ORIGIN can be processed safely
     * since the library's pathname came from a trusted source
     * (either system paths or a call to dlopen). */
    if (libc.secure)
      return 0;
    l = readlink("/proc/self/exe", buf, buf_size);
    if (l == -1) switch (errno) {
    case ENOENT:
    case ENOTDIR:
    case EACCES:
      break;
    default:
      return -1;
    }
    if (l >= buf_size)
      return 0;
    buf[l] = 0;
    origin = buf;
  } else {
    origin = p->name;
  }
  t = strrchr(origin, '/');
  l = t ? t-origin : 0;
  p->rpath = malloc(strlen(p->rpath_orig) + n*l + 1);
  if (!p->rpath) return -1;

  d = p->rpath;
  s = p->rpath_orig;
  while ((t=strchr(s, '$'))) {
    memcpy(d, s, t-s);
    d += t-s;
    memcpy(d, origin, l);
    d += l;
    /* It was determined previously that the '$' is followed
     * either by "ORIGIN" or "{ORIGIN}". */
    s = t + 7 + 2*(t[1]=='{');
  }
  strcpy(d, s);
  return 0;
}

static void decode_dyn(struct dso *p)
{
  size_t dyn[DYN_CNT];
  decode_vec(p->dynv, dyn, DYN_CNT);
  p->syms = laddr(p, dyn[DT_SYMTAB]);
  p->strings = laddr(p, dyn[DT_STRTAB]);
  if (dyn[0]&(1<<DT_HASH))
    p->hashtab = laddr(p, dyn[DT_HASH]);
  if (dyn[0]&(1<<DT_RPATH))
    p->rpath_orig = p->strings + dyn[DT_RPATH];
  if (dyn[0]&(1<<DT_RUNPATH))
    p->rpath_orig = p->strings + dyn[DT_RUNPATH];
  if (dyn[0]&(1<<DT_PLTGOT))
    p->got = laddr(p, dyn[DT_PLTGOT]);
  if (search_vec(p->dynv, dyn, DT_GNU_HASH))
    p->ghashtab = laddr(p, *dyn);
  if (search_vec(p->dynv, dyn, DT_VERSYM))
    p->versym = laddr(p, *dyn);
}

static size_t count_syms(struct dso *p)
{
  if (p->hashtab) return p->hashtab[1];

  size_t nsym, i;
  uint32_t *buckets = p->ghashtab + 4 + (p->ghashtab[2]*sizeof(size_t)/4);
  uint32_t *hashval;
  //Find the largest index of symbol table in buckets
  for (i = nsym = 0; i < p->ghashtab[0]; i++) {
    if (buckets[i] > nsym)
      nsym = buckets[i];
  }
  //Count number of chains in hash table
  if (nsym) {
    hashval = buckets + p->ghashtab[0] + (nsym - p->ghashtab[1]);
    do nsym++;
    while (!(*hashval++ & 1));
  }
  return nsym;
}

static void *dl_mmap(size_t n)
{
  void *p;
  int prot = PROT_READ|PROT_WRITE, flags = MAP_ANONYMOUS|MAP_PRIVATE;
#ifdef SYS_mmap2
  p = (void *)__syscall(SYS_mmap2, 0, n, prot, flags, -1, 0);
#else
  p = (void *)__syscall(SYS_mmap, 0, n, prot, flags, -1, 0);
#endif
  return p == MAP_FAILED ? 0 : p;
}

static void makefuncdescs(struct dso *p)
{
  static int self_done;
  size_t nsym = count_syms(p);
  size_t i, size = nsym * sizeof(*p->funcdescs);

  if (!self_done) {
    p->funcdescs = dl_mmap(size);
    self_done = 1;
  } else {
    p->funcdescs = malloc(size);
  }
  if (!p->funcdescs) {
    if (!runtime) a_crash();
    error("Error allocating function descriptors for %s", p->name);
    longjmp(*rtld_fail, 1);
  }
  for (i=0; i<nsym; i++) {
    if ((p->syms[i].st_info&0xf)==STT_FUNC && p->syms[i].st_shndx) {
      p->funcdescs[i].addr = laddr(p, p->syms[i].st_value);
      p->funcdescs[i].got = p->got;
    } else {
      p->funcdescs[i].addr = 0;
      p->funcdescs[i].got = 0;
    }
  }
}

static struct dso *load_library(const char *name, struct dso *needed_by)
{
  char buf[2*NAME_MAX+2];
  const char *pathname;
  unsigned char *map;
  struct dso *p, temp_dso = {0};
  int fd;
  struct stat st;
  size_t alloc_size;
  int n_th = 0;
  int is_self = 0;

  if (!*name) {
    errno = EINVAL;
    return 0;
  }

  /* Catch and block attempts to reload the implementation itself */
  if (name[0]=='l' && name[1]=='i' && name[2]=='b') {
    static const char *rp, reserved[] =
      "c\0pthread\0rt\0m\0dl\0util\0xnet\0";
    char *z = strchr(name, '.');
    if (z) {
      size_t l = z-name;
      for (rp=reserved; *rp && strncmp(name+3, rp, l-3); rp+=strlen(rp)+1);
      if (*rp) {
        if (ldd_mode) {
          /* Track which names have been resolved
           * and only report each one once. */
          static unsigned reported;
          unsigned mask = 1U<<(rp-reserved);
          if (!(reported & mask)) {
            reported |= mask;
            dprintf(1, "\t%s => %s (%p)\n",
              name, ldso.name,
              ldso.base);
          }
        }
        is_self = 1;
      }
    }
  }

  if (!strcmp(name, ldso.name)) is_self = 1;
  if (is_self) {
    if (!ldso.prev) {
      tail->next = &ldso;
      ldso.prev = tail;
      tail = ldso.next ? ldso.next : &ldso;
    }
    return &ldso;
  }
  if (strchr(name, '/')) {
    pathname = name;
    fd = open(name, O_RDONLY|O_CLOEXEC);
  } else {
    /* Search for the name to see if it's already loaded */
    for (p=head->next; p; p=p->next) {
      if (p->shortname && !strcmp(p->shortname, name)) {
        p->refcnt++;
        return p;
      }
    }
    if (strlen(name) > NAME_MAX) return 0;
    fd = -1;
    if (env_path) fd = path_open(name, env_path, buf, sizeof buf);
    for (p=needed_by; fd == -1 && p; p=p->needed_by) {
      if (fixup_rpath(p, buf, sizeof buf) < 0)
        fd = -2; /* Inhibit further search. */
      if (p->rpath)
        fd = path_open(name, p->rpath, buf, sizeof buf);
    }
    if (fd == -1) {
      if (!sys_path) {
        char *prefix = 0;
        size_t prefix_len;
        if (ldso.name[0]=='/') {
          char *s, *t, *z;
          for (s=t=z=ldso.name; *s; s++)
            if (*s=='/') z=t, t=s;
          prefix_len = z-ldso.name;
          if (prefix_len < PATH_MAX)
            prefix = ldso.name;
        }
        if (!prefix) {
          prefix = "";
          prefix_len = 0;
        }
        char etc_ldso_path[prefix_len + 1
          + sizeof "/etc/ld-musl-" LDSO_ARCH ".path"];
        snprintf(etc_ldso_path, sizeof etc_ldso_path,
          "%.*s/etc/ld-musl-" LDSO_ARCH ".path",
          (int)prefix_len, prefix);
        FILE *f = fopen(etc_ldso_path, "rbe");
        if (f) {
          if (getdelim(&sys_path, (size_t[1]){0}, 0, f) <= 0) {
            free(sys_path);
            sys_path = "";
          }
          fclose(f);
        } else if (errno != ENOENT) {
          sys_path = "";
        }
      }
      if (!sys_path) sys_path = "/lib:/usr/local/lib:/usr/lib";
      fd = path_open(name, sys_path, buf, sizeof buf);
    }
    pathname = buf;
  }
  if (fd < 0) return 0;
  if (fstat(fd, &st) < 0) {
    close(fd);
    return 0;
  }
  for (p=head->next; p; p=p->next) {
    if (p->dev == st.st_dev && p->ino == st.st_ino) {
      /* If this library was previously loaded with a
       * pathname but a search found the same inode,
       * setup its shortname so it can be found by name. */
      if (!p->shortname && pathname != name)
        p->shortname = strrchr(p->name, '/')+1;
      close(fd);
      p->refcnt++;
      return p;
    }
  }
  map = noload ? 0 : map_library(fd, &temp_dso);
  close(fd);
  if (!map) return 0;

  /* Allocate storage for the new DSO. When there is TLS, this
   * storage must include a reservation for all pre-existing
   * threads to obtain copies of both the new TLS, and an
   * extended DTV capable of storing an additional slot for
   * the newly-loaded DSO. */
  alloc_size = sizeof *p + strlen(pathname) + 1;
  if (runtime && temp_dso.tls.image) {
    size_t per_th = temp_dso.tls.size + temp_dso.tls.align
      + sizeof(void *) * (tls_cnt+3);
    n_th = libc.threads_minus_1 + 1;
    if (n_th > SSIZE_MAX / per_th) alloc_size = SIZE_MAX;
    else alloc_size += n_th * per_th;
  }
  p = calloc(1, alloc_size);
  if (!p) {
    unmap_library(&temp_dso);
    return 0;
  }
  memcpy(p, &temp_dso, sizeof temp_dso);
  decode_dyn(p);
  p->dev = st.st_dev;
  p->ino = st.st_ino;
  p->refcnt = 1;
  p->needed_by = needed_by;
  p->name = p->buf;
  strcpy(p->name, pathname);
  /* Add a shortname only if name arg was not an explicit pathname. */
  if (pathname != name) p->shortname = strrchr(p->name, '/')+1;
  if (p->tls.image) {
    p->tls_id = ++tls_cnt;
    tls_align = MAXP2(tls_align, p->tls.align);
#ifdef TLS_ABOVE_TP
    p->tls.offset = tls_offset + ( (tls_align-1) &
      -(tls_offset + (uintptr_t)p->tls.image) );
    tls_offset += p->tls.size;
#else
    tls_offset += p->tls.size + p->tls.align - 1;
    tls_offset -= (tls_offset + (uintptr_t)p->tls.image)
      & (p->tls.align-1);
    p->tls.offset = tls_offset;
#endif
    p->new_dtv = (void *)(-sizeof(size_t) &
      (uintptr_t)(p->name+strlen(p->name)+sizeof(size_t)));
    p->new_tls = (void *)(p->new_dtv + n_th*(tls_cnt+1));
    if (tls_tail) tls_tail->next = &p->tls;
    else libc.tls_head = &p->tls;
    tls_tail = &p->tls;
  }

  tail->next = p;
  p->prev = tail;
  tail = p;

  if (DL_FDPIC) makefuncdescs(p);

  if (ldd_mode) dprintf(1, "\t%s => %s (%p)\n", name, pathname, p->base);

  return p;
}

static void load_deps(struct dso *p)
{
  size_t i, ndeps=0;
  struct dso ***deps = &p->deps, **tmp, *dep;
  for (; p; p=p->next) {
    for (i=0; p->dynv[i]; i+=2) {
      if (p->dynv[i] != DT_NEEDED) continue;
      dep = load_library(p->strings + p->dynv[i+1], p);
      if (!dep) {
        error("Error loading shared library %s: %m (needed by %s)",
          p->strings + p->dynv[i+1], p->name);
        if (runtime) longjmp(*rtld_fail, 1);
        continue;
      }
      if (runtime) {
        tmp = realloc(*deps, sizeof(*tmp)*(ndeps+2));
        if (!tmp) longjmp(*rtld_fail, 1);
        tmp[ndeps++] = dep;
        tmp[ndeps] = 0;
        *deps = tmp;
      }
    }
  }
}

static void load_preload(char *s)
{
  int tmp;
  char *z;
  for (z=s; *z; s=z) {
    for (   ; *s && (isspace(*s) || *s==':'); s++);
    for (z=s; *z && !isspace(*z) && *z!=':'; z++);
    tmp = *z;
    *z = 0;
    load_library(s, 0);
    *z = tmp;
  }
}

static void make_global(struct dso *p)
{
  for (; p; p=p->next) p->global = 1;
}

static void do_mips_relocs(struct dso *p, size_t *got)
{
  size_t i, j, rel[2];
  unsigned char *base = p->base;
  i=0; search_vec(p->dynv, &i, DT_MIPS_LOCAL_GOTNO);
  if (p==&ldso) {
    got += i;
  } else {
    while (i--) *got++ += (size_t)base;
  }
  j=0; search_vec(p->dynv, &j, DT_MIPS_GOTSYM);
  i=0; search_vec(p->dynv, &i, DT_MIPS_SYMTABNO);
  Sym *sym = p->syms + j;
  rel[0] = (unsigned char *)got - base;
  for (i-=j; i; i--, sym++, rel[0]+=sizeof(size_t)) {
    rel[1] = R_INFO(sym-p->syms, R_MIPS_JUMP_SLOT);
    do_relocs(p, rel, sizeof rel, 2);
  }
}

static void reloc_all(struct dso *p)
{
  size_t dyn[DYN_CNT];
  for (; p; p=p->next) {
    if (p->relocated) continue;
    decode_vec(p->dynv, dyn, DYN_CNT);
    if (NEED_MIPS_GOT_RELOCS)
      do_mips_relocs(p, laddr(p, dyn[DT_PLTGOT]));
    do_relocs(p, laddr(p, dyn[DT_JMPREL]), dyn[DT_PLTRELSZ],
      2+(dyn[DT_PLTREL]==DT_RELA));
    do_relocs(p, laddr(p, dyn[DT_REL]), dyn[DT_RELSZ], 2);
    do_relocs(p, laddr(p, dyn[DT_RELA]), dyn[DT_RELASZ], 3);

    if (head != &ldso && p->relro_start != p->relro_end &&
        mprotect(laddr(p, p->relro_start), p->relro_end-p->relro_start, PROT_READ)
        && errno != ENOSYS) {
      error("Error relocating %s: RELRO protection failed: %m",
        p->name);
      if (runtime) longjmp(*rtld_fail, 1);
    }

    p->relocated = 1;
  }
}

static void kernel_mapped_dso(struct dso *p)
{
  size_t min_addr = -1, max_addr = 0, cnt;
  Phdr *ph = p->phdr;
  for (cnt = p->phnum; cnt--; ph = (void *)((char *)ph + p->phentsize)) {
    if (ph->p_type == PT_DYNAMIC) {
      p->dynv = laddr(p, ph->p_vaddr);
    } else if (ph->p_type == PT_GNU_RELRO) {
      p->relro_start = ph->p_vaddr & -PAGE_SIZE;
      p->relro_end = (ph->p_vaddr + ph->p_memsz) & -PAGE_SIZE;
    }
    if (ph->p_type != PT_LOAD) continue;
    if (ph->p_vaddr < min_addr)
      min_addr = ph->p_vaddr;
    if (ph->p_vaddr+ph->p_memsz > max_addr)
      max_addr = ph->p_vaddr+ph->p_memsz;
  }
  min_addr &= -PAGE_SIZE;
  max_addr = (max_addr + PAGE_SIZE-1) & -PAGE_SIZE;
  p->map = p->base + min_addr;
  p->map_len = max_addr - min_addr;
  p->kernel_mapped = 1;
}

void __libc_exit_fini()
{
  struct dso *p;
  size_t dyn[DYN_CNT];
  for (p=fini_head; p; p=p->fini_next) {
    if (!p->constructed) continue;
    decode_vec(p->dynv, dyn, DYN_CNT);
    if (dyn[0] & (1<<DT_FINI_ARRAY)) {
      size_t n = dyn[DT_FINI_ARRAYSZ]/sizeof(size_t);
      size_t *fn = (size_t *)laddr(p, dyn[DT_FINI_ARRAY])+n;
      while (n--) ((void (*)(void))*--fn)();
    }
#ifndef NO_LEGACY_INITFINI
    if ((dyn[0] & (1<<DT_FINI)) && dyn[DT_FINI])
      fpaddr(p, dyn[DT_FINI])();
#endif
  }
}

static void do_init_fini(struct dso *p)
{
  size_t dyn[DYN_CNT];
  int need_locking = libc.threads_minus_1;
  /* Allow recursive calls that arise when a library calls
   * dlopen from one of its constructors, but block any
   * other threads until all ctors have finished. */
  if (need_locking) pthread_mutex_lock(&init_fini_lock);
  for (; p; p=p->prev) {
    if (p->constructed) continue;
    p->constructed = 1;
    decode_vec(p->dynv, dyn, DYN_CNT);
    if (dyn[0] & ((1<<DT_FINI) | (1<<DT_FINI_ARRAY))) {
      p->fini_next = fini_head;
      fini_head = p;
    }
#ifndef NO_LEGACY_INITFINI
    if ((dyn[0] & (1<<DT_INIT)) && dyn[DT_INIT])
      fpaddr(p, dyn[DT_INIT])();
#endif
    if (dyn[0] & (1<<DT_INIT_ARRAY)) {
      size_t n = dyn[DT_INIT_ARRAYSZ]/sizeof(size_t);
      size_t *fn = laddr(p, dyn[DT_INIT_ARRAY]);
      while (n--) ((void (*)(void))*fn++)();
    }
    if (!need_locking && libc.threads_minus_1) {
      need_locking = 1;
      pthread_mutex_lock(&init_fini_lock);
    }
  }
  if (need_locking) pthread_mutex_unlock(&init_fini_lock);
}

void __libc_start_init(void)
{
  do_init_fini(tail);
}

static void dl_debug_state(void)
{
}

weak_alias(dl_debug_state, _dl_debug_state);

void __init_tls(size_t *auxv)
{
}

__attribute__((__visibility__("hidden")))
void *__tls_get_new(size_t *v)
{
  pthread_t self = __pthread_self();

  /* Block signals to make accessing new TLS async-signal-safe */
  sigset_t set;
  __block_all_sigs(&set);
  if (v[0]<=(size_t)self->dtv[0]) {
    __restore_sigs(&set);
    return (char *)self->dtv[v[0]]+v[1]+DTP_OFFSET;
  }

  /* This is safe without any locks held because, if the caller
   * is able to request the Nth entry of the DTV, the DSO list
   * must be valid at least that far out and it was synchronized
   * at program startup or by an already-completed call to dlopen. */
  struct dso *p;
  for (p=head; p->tls_id != v[0]; p=p->next);

  /* Get new DTV space from new DSO if needed */
  if (v[0] > (size_t)self->dtv[0]) {
    void **newdtv = p->new_dtv +
      (v[0]+1)*a_fetch_add(&p->new_dtv_idx,1);
    memcpy(newdtv, self->dtv,
      ((size_t)self->dtv[0]+1) * sizeof(void *));
    newdtv[0] = (void *)v[0];
    self->dtv = self->dtv_copy = newdtv;
  }

  /* Get new TLS memory from all new DSOs up to the requested one */
  unsigned char *mem;
  for (p=head; ; p=p->next) {
    if (!p->tls_id || self->dtv[p->tls_id]) continue;
    mem = p->new_tls + (p->tls.size + p->tls.align)
      * a_fetch_add(&p->new_tls_idx,1);
    mem += ((uintptr_t)p->tls.image - (uintptr_t)mem)
      & (p->tls.align-1);
    self->dtv[p->tls_id] = mem;
    memcpy(mem, p->tls.image, p->tls.len);
    if (p->tls_id == v[0]) break;
  }
  __restore_sigs(&set);
  return mem + v[1] + DTP_OFFSET;
}

static void update_tls_size()
{
  libc.tls_cnt = tls_cnt;
  libc.tls_align = tls_align;
  libc.tls_size = ALIGN(
    (1+tls_cnt) * sizeof(void *) +
    tls_offset +
    sizeof(struct pthread) +
    tls_align * 2,
  tls_align);

}

//Go through a dso's symtab and create a list of all its functions
static void add_func_from_dso(struct dso *p, int i) {
  funcinfo_t *newfunc, *func;
  func = newfunc = NULL;
  loadinfo_t *newload;
  pageinfo_t *newpage = NULL;
  char *name = p->strings + p->syms[i].st_name;

  HASH_FIND_STR(p->func_lst, name, func);
  if(func == NULL) {
    newfunc = calloc(1, sizeof(funcinfo_t));

    newfunc->name = name;
    newfunc->start_addr = laddr(p, p->syms[i].st_value);
    newfunc->weak = (p->syms[i].st_info>>4 == STB_WEAK) ? 1:0 ;
    HASH_ADD_KEYPTR(hh, p->func_lst, name, strlen(name), newfunc);

    //Add the new fn to code_page_lst
    void *page_num = (size_t) newfunc->start_addr & -PAGE_SIZE;
    HASH_FIND_PTR(p->code_page_lst, &page_num, newpage);
    if (!newpage) {
      newpage = calloc(1, sizeof(pageinfo_t));
      newpage->page_num = page_num;
      newpage->fn_count = 1;
      newpage->load_fn_count = 0;
      newpage->page_map = 0;
      HASH_ADD_PTR(p->code_page_lst, page_num, newpage);
    } else {
      newpage->fn_count++;
    }

    //Add the new fn to load_lst
    HASH_FIND_PTR(p->load_lst, &newfunc->start_addr, newload);
    if(newload == NULL) {
      //Add new function
      newload = calloc(1, sizeof(loadinfo_t));
      newload->start_addr = newfunc->start_addr;
      newload->func_sz = p->syms[i].st_size; //To be updated later
      newload->loaded = 0;
      newload->removed = 0;
      newload->global_name = newfunc->name;
      newload->pageinfo = newpage;
      HASH_ADD_PTR(p->load_lst, start_addr, newload);
    }else {
      // Function load info already in hashtable. This means newfunc is either
      // a weak or a global symbol of the existing function
      if(!newfunc->weak) {
        newload->global_name = newfunc->name;
      }
    }
  }
}

/* If we find new function in dep_list, add it to our func_lst
 * However, there will be no symbol binding information. We're assuming 
 * all are non weak symbols.
 */
static void add_func(struct dso *p, char *name, void *addr, size_t func_sz) {
  //dprintf(2, "%s\n", name);
  funcinfo_t *newfunc;
  loadinfo_t *newload;
  pageinfo_t *newpage;

  newfunc = calloc(1, sizeof(funcinfo_t));

  newfunc->name = name;
  newfunc->start_addr = addr;
  newfunc->weak = 0;

  //Add the new fn to code page list
  void *page_num = (size_t) newfunc->start_addr & -PAGE_SIZE;
  HASH_FIND_PTR(p->code_page_lst, &page_num, newpage);
  if (!newpage) {
    newpage = calloc(1, sizeof(pageinfo_t));
    newpage->page_num = page_num;
    newpage->load_fn_count = 0;
    newpage->fn_count = 1;
    HASH_ADD_PTR(p->code_page_lst, page_num, newpage);
  } else {
    newpage->fn_count++;
  }

  HASH_FIND_PTR(p->load_lst, &addr, newload);
  if(newload == NULL) {
    newload = calloc(1, sizeof(loadinfo_t));
    newload->start_addr = addr;
    newload->func_sz = func_sz;
    newload->loaded = 0;
    newload->removed = 0;
    newload->global_name = newfunc->name;
    newload->pageinfo = newpage;
    HASH_ADD_PTR(p->load_lst, start_addr, newload);
  } 
  HASH_ADD_KEYPTR(hh, p->func_lst, name, strlen(name), newfunc);
}

static void add_dep(dep_lst_t *new_list) {
  dep_lst_t *s;
  HASH_FIND_PTR(dep_graph, &new_list->start_addr, s);
  if(s == NULL) {
    HASH_ADD_PTR(dep_graph, start_addr, new_list);
  }
}

static void print_func_lst(struct dso *p) {
  funcinfo_t *f;
  loadinfo_t *l;

  for(f=p->func_lst; f != NULL; f=(funcinfo_t*)(f->hh.next)) {
    HASH_FIND_PTR(p->load_lst, &f->start_addr, l);
    dprintf(2, "%s,%p,%d,%s\n", f->name, l->start_addr, l->func_sz, l->global_name);
  }
}

static unsigned get_func_size(void *start_addr) {
  unsigned char *addr = (unsigned char *) start_addr;
  unsigned size = 0;
  loadinfo_t *load = NULL;

  HASH_FIND_PTR(ldso.load_lst, &addr, load);
  //Last function in library, below algorithm does not work
  if(strcmp(load->global_name, "__mulxc3") == 0)
    return 1011;

  load = NULL;
  while(!load) {
    addr++;
    size++;
    HASH_FIND_PTR(ldso.load_lst, &addr, load);
  }
  return size;
}

static void print_dep_list() {
  dep_lst_t *s;
  loadinfo_t *load;
  int i;
  for(s=dep_graph; s != NULL; s=(dep_lst_t *)(s->hh.next)) {
    dprintf(2, "%s %p: ", s->name, s->start_addr);
    for(i=0; i<s->nfuncs; i++) {
      dprintf(2, "%s ", s->funcs[i]->name);
    }
    dprintf(2, "\n");
  }
}

static void lookup_asm_func_info(dep_lst_t *lst) {
  funcinfo_t *func = NULL;
  HASH_FIND_STR(ldso.func_lst, lst->name, func);
  if(func) {
    lst->start_addr = func->start_addr;
    lst->func_sz = get_func_size(lst->start_addr);
  }else {
    fprintf(stderr, "Cannot find asm function %s\n", lst->name);
    return;
  }
}

static void lookup_func_info(dep_entry_t *entry) {
  funcinfo_t *func = NULL;
  loadinfo_t *load = NULL;
  HASH_FIND_STR(ldso.func_lst, entry->name, func);
  if(func) {
    entry->start_addr = func->start_addr;
    HASH_FIND_PTR(ldso.load_lst, &func->start_addr, load);
    if(load) entry->size = load->func_sz;
  }else {
    fprintf(stderr, "Cannot find function %s\n", entry->name);
    return;
  }
}
 
/* Read the contents of .dep section of a dso*/
static void decode_dep(struct dso *p, size_t depoff, size_t depsz)
{
  //Read .dep addr:
  size_t i;
  size_t size = 0;
  size_t func_count = 0;
  funcinfo_t *func = NULL;
  loadinfo_t *load = NULL;

  //Calculate addr of .dep section
  char *dep_saddr = (char *) p->base + depoff;
  char *dep_eaddr = (char *) dep_saddr + depsz;

  //i=0;
  size_t lst_sz, sz_count;
  lst_sz = sz_count;
  char* cur_addr = dep_saddr;

  //Read code pointers list
  size = *((size_t *) cur_addr);
  ldso.ncode_ptrs = size;
  cur_addr = (char *) cur_addr + sizeof(size_t);
  ldso.code_ptrs = calloc(size, sizeof(size_t));
  for(i=0; i<size; i++) {
    ldso.code_ptrs[i] = *((void **) cur_addr);
    cur_addr = (char *) cur_addr + sizeof(void *);
  }

  //Read depgraphs
  size = *((size_t *) cur_addr);
  cur_addr = (char *) cur_addr + sizeof(size_t);
  while (size-- > 0) {
    //Create new list
      dep_lst_t *new_list = (dep_lst_t *) calloc(1, sizeof(dep_lst_t));
      new_list->name = cur_addr;
      cur_addr += strlen(cur_addr) + 1;
      new_list->start_addr = *((void **) cur_addr);
      cur_addr = (char *) cur_addr + sizeof(void *);
      new_list->func_sz = *((size_t *) cur_addr);
      cur_addr = (char *) cur_addr + sizeof(size_t);
      func = NULL;
      load = NULL;
      HASH_FIND_STR(p->func_lst, new_list->name, func);
      if(func != NULL) {
        HASH_FIND_PTR(p->load_lst, &func->start_addr, load);
        load->func_sz = new_list->func_sz;
      } else {
        add_func(p, new_list->name, new_list->start_addr, new_list->func_sz);
      }
      //Read nfuncs in this list
      lst_sz = *((size_t *) cur_addr);
      sz_count = lst_sz;
      cur_addr = (char *)cur_addr + sizeof(size_t);
      new_list->nfuncs = lst_sz;
      new_list->funcs = (dep_entry_t **) calloc(lst_sz, sizeof(dep_entry_t *));
      int temp;
      for(temp=0; temp<lst_sz; temp++) {
        new_list->funcs[temp] = (dep_entry_t *) calloc(1, sizeof(dep_entry_t));
        new_list->funcs[temp]->name = cur_addr;
        cur_addr = (char *) cur_addr + strlen(cur_addr) + 1;
        new_list->funcs[temp]->start_addr = *((void **) cur_addr);
        cur_addr = (char *) cur_addr + sizeof(void *);
        new_list->funcs[temp]->size = *((size_t *) cur_addr);
        cur_addr = (char *) cur_addr + sizeof(size_t);
        func = NULL;
        load = NULL;
        HASH_FIND_STR(p->func_lst, new_list->funcs[temp]->name, func);
        if(func != NULL) {
          load = NULL;
          HASH_FIND_PTR(p->load_lst, &func->start_addr, load);
          load->func_sz = new_list->funcs[temp]->size;
        }else {
          add_func(p, new_list->funcs[temp]->name, new_list->funcs[temp]->start_addr
            , new_list->funcs[temp]->size);
        }
      }
      add_dep(new_list);
  }

  //Read graphs for asm functions
  size = *((size_t *) cur_addr);
  cur_addr = (char *) cur_addr + sizeof(size_t);
  func = NULL;
  load = NULL;
  while(size-- > 0) {
    //Read name of asm function
    dep_lst_t *new_list = (dep_lst_t *) calloc(1, sizeof(dep_lst_t));
    new_list->name = cur_addr;
    cur_addr += strlen(cur_addr) + 1;
    //Find address of asm function
    lookup_asm_func_info(new_list);
    //Read nfunc
    lst_sz = *((size_t *) cur_addr);
    //Read callees
    sz_count = 0;
    cur_addr = (char *)cur_addr + sizeof(size_t);
    new_list->nfuncs = lst_sz;
    new_list->funcs = (dep_entry_t **) calloc(lst_sz, sizeof(dep_entry_t *));
    for(; sz_count < lst_sz; sz_count++) {
      new_list->funcs[sz_count] = (dep_entry_t *) calloc(1, sizeof(dep_entry_t));
      //Read name
      new_list->funcs[sz_count]->name = cur_addr;
      cur_addr = (char *) cur_addr + strlen(cur_addr) + 1;
      lookup_func_info(new_list->funcs[sz_count]);
    }
    add_dep(new_list);
  }
}

static void cleanup() {
}

// Read ldso's symbol table, then create a list of all funcs in ldso
static void read_dso_func(struct dso *p) {
  size_t nsym, i;
  nsym = count_syms(p);
  for (i=0; i<nsym; i++) {
    // Read symbols, exclude imported functions (Imported functions have st_value of 0); exclude weak undefined symbols
    if (((p->syms[i].st_info&0xf) == STT_FUNC) && ((p->syms[i].st_value != 0))) {
      add_func_from_dso(&ldso, i);
    }
  }
}

static void load_func(struct dso *p, char *name) {
  funcinfo_t *func = NULL;
  loadinfo_t *load = NULL;

  HASH_FIND_STR(p->func_lst, name, func);
  if(func != NULL) {
    HASH_FIND_PTR(p->load_lst, &func->start_addr, load);
    if(!load->loaded) {
      load->loaded = 1;
      update_page_info(load);
      load_dep_funcs(p, load->start_addr);
    }
  }
}

static void read_import_tables(struct dso *app) {
  /* At this stage, we know all dso we need. This dso list is a 
   * a doubly linked list of type struct dso. Head ptr = head;
   * tail ptr = tail. */
  size_t nsym, i;
  struct dso *p;
  for (p=head; p; p=p->next) {
    //Skip ldso
    if(p == &ldso) continue;
    int num_import = 0;
    nsym = count_syms(p);
    for (i=0; i<nsym; i++) {
      //Read import function from symbol tables only. Imported entries have value 0
      if (((p->syms[i].st_info&0xf) == STT_FUNC)/* && (p->syms[i].st_value == 0)*/) {
        char *name = p->strings + p->syms[i].st_name;
        load_func(&ldso, name);
      }
    }
  }

  //Load functions in app's import table
  nsym = count_syms(app);
  for (i=0; i<nsym; i++) {
    //Read import function from symbol tables only. Imported entries have value 0
    if (((app->syms[i].st_info&0xf) == STT_FUNC) && (app->syms[i].st_value == 0)) {
      char *name = app->strings + app->syms[i].st_name;
      load_func(&ldso, name);
    }
  }
}

static void load_must_have() {
  funcinfo_t *func = NULL;
  loadinfo_t *load = NULL;
  int i;

  for(i=0; i<MHSZ; i++) {
    load_func(&ldso, must_have[i]);
  }

  load_func(&ldso, "__dls3");

  load = NULL;
  for(i=0; i<ldso.ncode_ptrs; i++) {
    HASH_FIND_PTR(ldso.load_lst, &ldso.code_ptrs[i], load);
    if(!load->loaded) {
      load->loaded = 1;
      //dprintf(2, "Load must have %s\n", load->global_name);
      load_dep_funcs(&ldso, ldso.code_ptrs[i]);

      //Update fn counter in codepage
      update_page_info(load);
    }
  }

}

static void *cpy_memset() {
  funcinfo_t *func = NULL;
  unsigned char *new_memset, *start_addr, *p;
  new_memset = start_addr = p = NULL;
  int size = 0;

  HASH_FIND_STR(ldso.func_lst, "memset", func);
  if(func != NULL) {
    start_addr = func->start_addr;
    new_memset = memalign(PAGE_SIZE, PAGE_SIZE);

    //size = 195;
    size = get_func_size(start_addr);

    memcpy(new_memset, start_addr, size);
    start_addr += size;
    mprotect(new_memset, PAGE_SIZE, PROT_READ | PROT_EXEC);
  }
  return new_memset;
}

static void test_memset() {
  void * (*new_memset) (void *, int, size_t) = NULL;
  char *p = NULL;
  new_memset = cpy_memset();
  char *buffer = calloc(0, 34);
  memcpy(buffer, "Test string. Memset copy succeed!", 34);
  new_memset(buffer, 'a', 11);
  dprintf(2, "%s\n", buffer);
}

static void *cpy_mprotect() {
  funcinfo_t *func = NULL;
  loadinfo_t *load = NULL;
  unsigned char * start_addr = NULL;
  char *new_mprotect = NULL;
  char *new_mprotect_end = NULL;
  char *new_syscall3_633 = NULL;
  char *new_syscall_ret = NULL;
  unsigned char *p = NULL;
  int size = 0;

  HASH_FIND_STR(ldso.func_lst, "__mprotect", func);
  if(func != NULL) {
    HASH_FIND_PTR(ldso.load_lst, &func->start_addr, load);
    start_addr = func->start_addr;
    new_mprotect = memalign(PAGE_SIZE, PAGE_SIZE);

    size = load->func_sz;
    memcpy(new_mprotect, start_addr, load->func_sz);
    start_addr += size;

    new_mprotect_end = new_mprotect + size;
    size = 2;
    memcpy(new_mprotect_end, start_addr, size);
    start_addr += size;

    new_syscall3_633 = new_mprotect_end + size;
    size = 0;
    for(p= ((unsigned char *) start_addr); (*p) != 0xc3; p++){ 
      size++; 
    }
    size++;
    memcpy(new_syscall3_633, start_addr, size);

    new_syscall_ret = new_syscall3_633 + size;
    HASH_FIND_STR(ldso.func_lst, "__syscall_ret", func);
    HASH_FIND_PTR(ldso.load_lst, &func->start_addr, load);
    size = load->func_sz;
    start_addr = func->start_addr;
    memcpy(new_syscall_ret, start_addr, size);

    //Change the call offset in new copy of mprotect
    new_mprotect[110] = 0x3e;
    new_mprotect[111] = 0;
    new_mprotect[112] = 0;
    new_mprotect[113] = 0;

    //Remove the call to syscall_ret in syscall3.633, now we do not have to 
    //copy __errno_locations
    new_syscall_ret[35] = 0x90;
    new_syscall_ret[36] = 0x90;
    new_syscall_ret[37] = 0x90;
    new_syscall_ret[38] = 0x90;
    new_syscall_ret[39] = 0x90;

    //Change new page permission to executable
    mprotect(new_mprotect, PAGE_SIZE, PROT_READ | PROT_EXEC);
  }
  return new_mprotect;
}

static void *find_mprotect() {
  funcinfo_t *func = NULL;
  HASH_FIND_STR(ldso.func_lst, "mprotect", func);
  if(func != NULL) {
    return func->start_addr;
  }
  return NULL;
}

static void test_mprotect() {
  int (*new_mprotect) (void *, size_t, int) = NULL;
  char *p;
  new_mprotect = cpy_mprotect();
  char * buffer = memalign(PAGE_SIZE, PAGE_SIZE);
  new_mprotect(buffer, PAGE_SIZE, PROT_READ);
  for (p = buffer ;p<buffer+PAGE_SIZE ; )
    *(p++) = 'a';
  dprintf(2, "Failed to copy mprotect");
}

static void remove_code_pages() {
  pageinfo_t *pageinfo = NULL;

  for(pageinfo=ldso.code_page_lst; pageinfo != NULL; pageinfo=(pageinfo->hh.next)) {
    if (pageinfo->load_fn_count == 0) {
      dprintf(2, "Remove code page %lx %d\n", pageinfo->page_num, pageinfo->fn_count);
      ldso.mprotect_stub(pageinfo->page_num, PAGE_SIZE, PROT_NONE);
    }
  }
}

static void remove_func() {
  loadinfo_t *load = NULL;
  void *end_addr = 0;
  int i = 0;
  void *start_addr = &remove_func;

  HASH_FIND_PTR(ldso.load_lst, &start_addr, load);
  end_addr = (void *) ((char *)start_addr + load->func_sz);

  for(load=ldso.load_lst; load != NULL; load=(load->hh.next)) {
    if((load->loaded == 0)) {
      if(load->func_sz == 0) continue;
      size_t remove_page_begin = (size_t)load->start_addr & -PAGE_SIZE;
      size_t remove_page_end = (size_t)((char *)load->start_addr + load->func_sz) & -PAGE_SIZE;
      size_t current_page_begin = (size_t) start_addr & -PAGE_SIZE;
      size_t current_page_end = (size_t) end_addr & -PAGE_SIZE;
      if((remove_page_begin == current_page_begin) || (remove_page_begin == current_page_end)){
        ldso.remove_list[i] = load->start_addr;
        i++;
        continue;
      }
      if(remove_page_end >= current_page_begin && remove_page_end <= current_page_end){
        ldso.remove_list[i] = load->start_addr;
        i++;
        continue;
      }
      if((load->removed == 0) && ldso.mprotect_stub(load->start_addr, load->func_sz, PROT_READ | PROT_WRITE) == 0){
        ldso.memset_stub(load->start_addr, 0xd6, load->func_sz);
        ldso.mprotect_stub(load->start_addr, load->func_sz, PROT_READ | PROT_EXEC);
        load->removed = 1;
      }
    }
  }

  //Add remove_func to remove later
  char *name = "remove_func";
  funcinfo_t *func = NULL;
  HASH_FIND_STR(ldso.func_lst, name, func);
  if(func != NULL) {
    ldso.remove_list[i] = func->start_addr;
  }
}

/* Stage 1 of the dynamic linker is defined in dlstart.c. It calls the
 * following stage 2 and stage 3 functions via primitive symbolic lookup
 * since it does not have access to their addresses to begin with. */

/* Stage 2 of the dynamic linker is called after relative relocations 
 * have been processed. It can make function calls to static functions
 * and access string literals and static data, but cannot use extern
 * symbols. Its job is to perform symbolic relocations on the dynamic
 * linker itself, but some of the relocations performed may need to be
 * replaced later due to copy relocations in the main program. */

__attribute__((__visibility__("hidden")))
void __dls2(unsigned char *base, size_t *sp)
{
  if (DL_FDPIC) {
    void *p1 = (void *)sp[-2];
    void *p2 = (void *)sp[-1];
    if (!p1) {
      size_t *auxv, aux[AUX_CNT];
      for (auxv=sp+1+*sp+1; *auxv; auxv++); auxv++;
      decode_vec(auxv, aux, AUX_CNT);
      if (aux[AT_BASE]) ldso.base = (void *)aux[AT_BASE];
      else ldso.base = (void *)(aux[AT_PHDR] & -4096);
    }
    app_loadmap = p2 ? p1 : 0;
    ldso.loadmap = p2 ? p2 : p1;
    ldso.base = laddr(&ldso, 0);
  } else {
    ldso.base = base;
  }
  Ehdr *ehdr = (void *)ldso.base;
  ldso.name = ldso.shortname = "libc.so";
  ldso.global = 1;
  ldso.phnum = ehdr->e_phnum;
  ldso.shnum = ehdr->e_shnum;
  ldso.shdr = laddr(&ldso, ehdr->e_shoff);
  ldso.phdr = laddr(&ldso, ehdr->e_phoff);
  ldso.phentsize = ehdr->e_phentsize;
  ldso.shentsize = ehdr->e_shentsize;
  int i = 0;
  for(; i<10; i++) {
    ldso.remove_list[i] = NULL;
  }
  
  kernel_mapped_dso(&ldso);
  decode_dyn(&ldso);

  if (DL_FDPIC) makefuncdescs(&ldso);

  /* Prepare storage for to save clobbered REL addends so they
   * can be reused in stage 3. There should be very few. If
   * something goes wrong and there are a huge number, abort
   * instead of risking stack overflow. */
  size_t dyn[DYN_CNT];
  decode_vec(ldso.dynv, dyn, DYN_CNT);
  size_t *rel = laddr(&ldso, dyn[DT_REL]);
  size_t rel_size = dyn[DT_RELSZ];
  size_t symbolic_rel_cnt = 0;
  apply_addends_to = rel;
  for (; rel_size; rel+=2, rel_size-=2*sizeof(size_t))
    if (!IS_RELATIVE(rel[1], ldso.syms)) symbolic_rel_cnt++;
  if (symbolic_rel_cnt >= ADDEND_LIMIT) a_crash();
  size_t addends[symbolic_rel_cnt+1];
  saved_addends = addends;

  head = &ldso;
  reloc_all(&ldso);

  ldso.relocated = 0;

  read_dso_func(&ldso);
  decode_dep(&ldso, DEPOFF, DEPSZ);

  /* Call dynamic linker stage-3, __dls3, looking it up
   * symbolically as a barrier against moving the address
   * load across the above relocation processing. */
  struct symdef dls3_def = find_sym(&ldso, "__dls3", 0);
  if (DL_FDPIC) ((stage3_func)&ldso.funcdescs[dls3_def.sym-ldso.syms])(sp);
  else ((stage3_func)laddr(&ldso, dls3_def.sym->st_value))(sp);
}

/* Stage 3 of the dynamic linker is called with the dynamic linker/libc
 * fully functional. Its job is to load (if not already loaded) and
 * process dependencies and relocations for the main application and
 * transfer control to its entry point. */

_Noreturn void __dls3(size_t *sp)
{
  static struct dso app, vdso;
  size_t aux[AUX_CNT], *auxv;
  size_t i;
  char *env_preload=0;
  size_t vdso_base;
  int argc = *sp;
  char **argv = (void *)(sp+1);
  char **argv_orig = argv;
  char **envp = argv+argc+1;

  /* Find aux vector just past environ[] and use it to initialize
   * global data that may be needed before we can make syscalls. */
  __environ = envp;
  for (i=argc+1; argv[i]; i++);
  libc.auxv = auxv = (void *)(argv+i+1);
  decode_vec(auxv, aux, AUX_CNT);
  __hwcap = aux[AT_HWCAP];
  libc.page_size = aux[AT_PAGESZ];
  libc.secure = ((aux[0]&0x7800)!=0x7800 || aux[AT_UID]!=aux[AT_EUID]
    || aux[AT_GID]!=aux[AT_EGID] || aux[AT_SECURE]);

  /* Setup early thread pointer in builtin_tls for ldso/libc itself to
   * use during dynamic linking. If possible it will also serve as the
   * thread pointer at runtime. */
  libc.tls_size = sizeof builtin_tls;
  libc.tls_align = tls_align;
  if (__init_tp(__copy_tls((void *)builtin_tls)) < 0) {
    a_crash();
  }

  /* Only trust user/env if kernel says we're not suid/sgid */
  if (!libc.secure) {
    env_path = getenv("LD_LIBRARY_PATH");
    env_preload = getenv("LD_PRELOAD");
  }

  /* If the main program was already loaded by the kernel,
   * AT_PHDR will point to some location other than the dynamic
   * linker's program headers. */
  if (aux[AT_PHDR] != (size_t)ldso.phdr) {
    size_t interp_off = 0;
    size_t tls_image = 0;
    /* Find load address of the main program, via AT_PHDR vs PT_PHDR. */
    Phdr *phdr = app.phdr = (void *)aux[AT_PHDR];
    app.phnum = aux[AT_PHNUM];
    app.phentsize = aux[AT_PHENT];
    for (i=aux[AT_PHNUM]; i; i--, phdr=(void *)((char *)phdr + aux[AT_PHENT])) {
      if (phdr->p_type == PT_PHDR)
        app.base = (void *)(aux[AT_PHDR] - phdr->p_vaddr);
      else if (phdr->p_type == PT_INTERP)
        interp_off = (size_t)phdr->p_vaddr;
      else if (phdr->p_type == PT_TLS) {
        tls_image = phdr->p_vaddr;
        app.tls.len = phdr->p_filesz;
        app.tls.size = phdr->p_memsz;
        app.tls.align = phdr->p_align;
      }
    }
    if (DL_FDPIC) app.loadmap = app_loadmap;
    if (app.tls.size) app.tls.image = laddr(&app, tls_image);
    if (interp_off) ldso.name = laddr(&app, interp_off);
    if ((aux[0] & (1UL<<AT_EXECFN))
        && strncmp((char *)aux[AT_EXECFN], "/proc/", 6))
      app.name = (char *)aux[AT_EXECFN];
    else
      app.name = argv[0];
    kernel_mapped_dso(&app);
  } else {
    int fd;
    char *ldname = argv[0];
    size_t l = strlen(ldname);
    if (l >= 3 && !strcmp(ldname+l-3, "ldd")) ldd_mode = 1;
    argv++;
    while (argv[0] && argv[0][0]=='-' && argv[0][1]=='-') {
      char *opt = argv[0]+2;
      *argv++ = (void *)-1;
      if (!*opt) {
        break;
      } else if (!memcmp(opt, "list", 5)) {
        ldd_mode = 1;
      } else if (!memcmp(opt, "library-path", 12)) {
        if (opt[12]=='=') env_path = opt+13;
        else if (opt[12]) *argv = 0;
        else if (*argv) env_path = *argv++;
      } else if (!memcmp(opt, "preload", 7)) {
        if (opt[7]=='=') env_preload = opt+8;
        else if (opt[7]) *argv = 0;
        else if (*argv) env_preload = *argv++;
      } else {
        argv[0] = 0;
      }
    }
    argv[-1] = (void *)(argc - (argv-argv_orig));
    if (!argv[0]) {
      dprintf(2, "musl libc (" LDSO_ARCH ")\n"
        "Version %s\n"
        "Dynamic Program Loader\n"
        "Usage: %s [options] [--] pathname%s\n",
        __libc_get_version(), ldname,
        ldd_mode ? "" : " [args]");
      _exit(1);
    }
    fd = open(argv[0], O_RDONLY);
    if (fd < 0) {
      dprintf(2, "%s: cannot load %s: %s\n", ldname, argv[0], strerror(errno));
      _exit(1);
    }
    runtime = 1;
    Ehdr *ehdr = (void *)map_library(fd, &app);
    if (!ehdr) {
      dprintf(2, "%s: %s: Not a valid dynamic program\n", ldname, argv[0]);
      _exit(1);
    }
    runtime = 0;
    close(fd);
    ldso.name = ldname;
    app.name = argv[0];
    aux[AT_ENTRY] = (size_t)laddr(&app, ehdr->e_entry);
    /* Find the name that would have been used for the dynamic
     * linker had ldd not taken its place. */
    if (ldd_mode) {
      for (i=0; i<app.phnum; i++) {
        if (app.phdr[i].p_type == PT_INTERP)
          ldso.name = laddr(&app, app.phdr[i].p_vaddr);
      }
      dprintf(1, "\t%s (%p)\n", ldso.name, ldso.base);
    }
  }
  if (app.tls.size) {
    libc.tls_head = tls_tail = &app.tls;
    app.tls_id = tls_cnt = 1;
#ifdef TLS_ABOVE_TP
    app.tls.offset = 0;
    tls_offset = app.tls.size
      + ( -((uintptr_t)app.tls.image + app.tls.size)
      & (app.tls.align-1) );
#else
    tls_offset = app.tls.offset = app.tls.size
      + ( -((uintptr_t)app.tls.image + app.tls.size)
      & (app.tls.align-1) );
#endif
    tls_align = MAXP2(tls_align, app.tls.align);
  }
  app.global = 1;
  decode_dyn(&app);
  
  if (DL_FDPIC) {
    makefuncdescs(&app);
    if (!app.loadmap) {
      app.loadmap = (void *)&app_dummy_loadmap;
      app.loadmap->nsegs = 1;
      app.loadmap->segs[0].addr = (size_t)app.map;
      app.loadmap->segs[0].p_vaddr = (size_t)app.map
        - (size_t)app.base;
      app.loadmap->segs[0].p_memsz = app.map_len;
    }
    argv[-3] = (void *)app.loadmap;
  }

  /* Attach to vdso, if provided by the kernel */
  if (search_vec(auxv, &vdso_base, AT_SYSINFO_EHDR)) {
    Ehdr *ehdr = (void *)vdso_base;
    Phdr *phdr = vdso.phdr = (void *)(vdso_base + ehdr->e_phoff);
    vdso.phnum = ehdr->e_phnum;
    vdso.phentsize = ehdr->e_phentsize;
    for (i=ehdr->e_phnum; i; i--, phdr=(void *)((char *)phdr + ehdr->e_phentsize)) {
      if (phdr->p_type == PT_DYNAMIC)
        vdso.dynv = (void *)(vdso_base + phdr->p_offset);
      if (phdr->p_type == PT_LOAD)
        vdso.base = (void *)(vdso_base - phdr->p_vaddr + phdr->p_offset);
    }
    vdso.name = "";
    vdso.shortname = "linux-gate.so.1";
    vdso.global = 1;
    vdso.relocated = 1;
    decode_dyn(&vdso);
    vdso.prev = &ldso;
    ldso.next = &vdso;
  }

  /* Initial dso chain consists only of the app. */
  head = tail = &app;

  /* Donate unused parts of app and library mapping to malloc */
  reclaim_gaps(&app);
  reclaim_gaps(&ldso);

  /* Load preload/needed libraries, add their symbols to the global
   * namespace, and perform all remaining relocations. */
  if (env_preload) load_preload(env_preload);
  load_deps(&app);
  make_global(&app);

  for (i=0; app.dynv[i]; i+=2) {
    if (!DT_DEBUG_INDIRECT && app.dynv[i]==DT_DEBUG)
      app.dynv[i+1] = (size_t)&debug;
    if (DT_DEBUG_INDIRECT && app.dynv[i]==DT_DEBUG_INDIRECT) {
      size_t *ptr = (size_t *) app.dynv[i+1];
      *ptr = (size_t)&debug;
    }
  }


  /* The main program must be relocated LAST since it may contin
   * copy relocations which depend on libraries' relocations. */
  reloc_all(app.next);
  reloc_all(&app);

  update_tls_size();
  if (libc.tls_size > sizeof builtin_tls || tls_align > MIN_TLS_ALIGN) {
    void *initial_tls = calloc(libc.tls_size, 1);
    if (!initial_tls) {
      dprintf(2, "%s: Error getting %zu bytes thread-local storage: %m\n",
        argv[0], libc.tls_size);
      _exit(127);
    }
    if (__init_tp(__copy_tls(initial_tls)) < 0) {
      a_crash();
    }
  } else {
    size_t tmp_tls_size = libc.tls_size;
    pthread_t self = __pthread_self();
    /* Temporarily set the tls size to the full size of
     * builtin_tls so that __copy_tls will use the same layout
     * as it did for before. Then check, just to be safe. */
    libc.tls_size = sizeof builtin_tls;
    if (__copy_tls((void*)builtin_tls) != self) a_crash();
    libc.tls_size = tmp_tls_size;
  }
  static_tls_cnt = tls_cnt;

  if (ldso_fail) _exit(127);
  if (ldd_mode) _exit(0);

  /* Switch to runtime mode: any further failures in the dynamic
   * linker are a reportable failure rather than a fatal startup
   * error. */
  runtime = 1;

  debug.ver = 1;
  debug.bp = dl_debug_state;
  debug.head = head;
  debug.base = ldso.base;
  debug.state = 0;
  _dl_debug_state();

  errno = 0;

  load_must_have();

  //Create a copy of mprotect and memcpy
  ldso.mprotect_stub = cpy_mprotect();
  ldso.memset_stub = cpy_memset();
  remove_func();

  loadinfo_t *load = NULL;
  void *addr = NULL;
  addr = &load_must_have;
  i = 0;

  for(; i<10; i++) {
    addr = ldso.remove_list[i];
    if(!addr) break;
    HASH_FIND_PTR(ldso.load_lst, &addr, load);
    if(ldso.mprotect_stub(load->start_addr, load->func_sz, PROT_READ | PROT_WRITE) == 0) {
      ldso.memset_stub(load->start_addr, 0xd6, load->func_sz);
      ldso.mprotect_stub(load->start_addr, load->func_sz, PROT_READ | PROT_EXEC);
      load->removed = 1;
    }
  }
  CRTJMP((void *)aux[AT_ENTRY], argv-1);
  for(;;);
}

void *dlopen(const char *file, int mode)
{
  struct dso *volatile p, *orig_tail, *next;
  struct tls_module *orig_tls_tail;
  size_t orig_tls_cnt, orig_tls_offset, orig_tls_align;
  size_t i;
  int cs;
  jmp_buf jb;

  if (!file) return head;

  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cs);
  pthread_rwlock_wrlock(&lock);
  __inhibit_ptc();

  p = 0;
  orig_tls_tail = tls_tail;
  orig_tls_cnt = tls_cnt;
  orig_tls_offset = tls_offset;
  orig_tls_align = tls_align;
  orig_tail = tail;
  noload = mode & RTLD_NOLOAD;

  rtld_fail = &jb;
  if (setjmp(*rtld_fail)) {
    /* Clean up anything new that was (partially) loaded */
    if (p && p->deps) for (i=0; p->deps[i]; i++)
      if (p->deps[i]->global < 0)
        p->deps[i]->global = 0;
    for (p=orig_tail->next; p; p=next) {
      next = p->next;
      while (p->td_index) {
        void *tmp = p->td_index->next;
        free(p->td_index);
        p->td_index = tmp;
      }
      free(p->funcdescs);
      if (p->rpath != p->rpath_orig)
        free(p->rpath);
      free(p->deps);
      unmap_library(p);
      free(p);
    }
    if (!orig_tls_tail) libc.tls_head = 0;
    tls_tail = orig_tls_tail;
    tls_cnt = orig_tls_cnt;
    tls_offset = orig_tls_offset;
    tls_align = orig_tls_align;
    tail = orig_tail;
    tail->next = 0;
    p = 0;
    goto end;
  } else p = load_library(file, head);

  if (!p) {
    error(noload ?
      "Library %s is not already loaded" :
      "Error loading shared library %s: %m",
      file);
    goto end;
  }

  /* First load handling */
  if (!p->deps) {
    load_deps(p);
    if (p->deps) for (i=0; p->deps[i]; i++)
      if (!p->deps[i]->global)
        p->deps[i]->global = -1;
    if (!p->global) p->global = -1;
    reloc_all(p);
    if (p->deps) for (i=0; p->deps[i]; i++)
      if (p->deps[i]->global < 0)
        p->deps[i]->global = 0;
    if (p->global < 0) p->global = 0;
  }

  if (mode & RTLD_GLOBAL) {
    if (p->deps) for (i=0; p->deps[i]; i++)
      p->deps[i]->global = 1;
    p->global = 1;
  }

  update_tls_size();
  _dl_debug_state();
  orig_tail = tail;
end:
  __release_ptc();
  if (p) gencnt++;
  pthread_rwlock_unlock(&lock);
  if (p) do_init_fini(orig_tail);
  pthread_setcancelstate(cs, 0);
  return p;
}

__attribute__((__visibility__("hidden")))
int __dl_invalid_handle(void *h)
{
  struct dso *p;
  for (p=head; p; p=p->next) if (h==p) return 0;
  error("Invalid library handle %p", (void *)h);
  return 1;
}

static void *addr2dso(size_t a)
{
  struct dso *p;
  size_t i;
  if (DL_FDPIC) for (p=head; p; p=p->next) {
    i = count_syms(p);
    if (a-(size_t)p->funcdescs < i*sizeof(*p->funcdescs))
      return p;
  }
  for (p=head; p; p=p->next) {
    if (DL_FDPIC && p->loadmap) {
      for (i=0; i<p->loadmap->nsegs; i++) {
        if (a-p->loadmap->segs[i].p_vaddr
            < p->loadmap->segs[i].p_memsz)
          return p;
      }
    } else {
      if (a-(size_t)p->map < p->map_len)
        return p;
    }
  }
  return 0;
}

void *__tls_get_addr(size_t *);

static void *do_dlsym(struct dso *p, const char *s, void *ra)
{
  size_t i;
  uint32_t h = 0, gh = 0, *ght;
  Sym *sym;
  if (p == head || p == RTLD_DEFAULT || p == RTLD_NEXT) {
    if (p == RTLD_DEFAULT) {
      p = head;
    } else if (p == RTLD_NEXT) {
      p = addr2dso((size_t)ra);
      if (!p) p=head;
      p = p->next;
    }
    struct symdef def = find_sym(p, s, 0);
    if (!def.sym) goto failed;
    if ((def.sym->st_info&0xf) == STT_TLS)
      return __tls_get_addr((size_t []){def.dso->tls_id, def.sym->st_value});
    if (DL_FDPIC && (def.sym->st_info&0xf) == STT_FUNC)
      return def.dso->funcdescs + (def.sym - def.dso->syms);
    return laddr(def.dso, def.sym->st_value);
  }
  if (__dl_invalid_handle(p))
    return 0;
  if ((ght = p->ghashtab)) {
    gh = gnu_hash(s);
    sym = gnu_lookup(gh, ght, p, s);
  } else {
    h = sysv_hash(s);
    sym = sysv_lookup(s, h, p);
  }
  if (sym && (sym->st_info&0xf) == STT_TLS)
    return __tls_get_addr((size_t []){p->tls_id, sym->st_value});
  if (DL_FDPIC && sym && sym->st_shndx && (sym->st_info&0xf) == STT_FUNC)
    return p->funcdescs + (sym - p->syms);
  if (sym && sym->st_value && (1<<(sym->st_info&0xf) & OK_TYPES))
    return laddr(p, sym->st_value);
  if (p->deps) for (i=0; p->deps[i]; i++) {
    if ((ght = p->deps[i]->ghashtab)) {
      if (!gh) gh = gnu_hash(s);
      sym = gnu_lookup(gh, ght, p->deps[i], s);
    } else {
      if (!h) h = sysv_hash(s);
      sym = sysv_lookup(s, h, p->deps[i]);
    }
    if (sym && (sym->st_info&0xf) == STT_TLS)
      return __tls_get_addr((size_t []){p->deps[i]->tls_id, sym->st_value});
    if (DL_FDPIC && sym && sym->st_shndx && (sym->st_info&0xf) == STT_FUNC)
      return p->deps[i]->funcdescs + (sym - p->deps[i]->syms);
    if (sym && sym->st_value && (1<<(sym->st_info&0xf) & OK_TYPES))
      return laddr(p->deps[i], sym->st_value);
  }
failed:
  error("Symbol not found: %s", s);
  return 0;
}

int dladdr(const void *addr, Dl_info *info)
{
  struct dso *p;
  Sym *sym, *bestsym;
  uint32_t nsym;
  char *strings;
  void *best = 0;

  pthread_rwlock_rdlock(&lock);
  p = addr2dso((size_t)addr);
  pthread_rwlock_unlock(&lock);

  if (!p) return 0;

  sym = p->syms;
  strings = p->strings;
  nsym = count_syms(p);

  if (DL_FDPIC) {
    size_t idx = ((size_t)addr-(size_t)p->funcdescs)
      / sizeof(*p->funcdescs);
    if (idx < nsym && (sym[idx].st_info&0xf) == STT_FUNC) {
      best = p->funcdescs + idx;
      bestsym = sym + idx;
    }
  }

  if (!best) for (; nsym; nsym--, sym++) {
    if (sym->st_value
     && (1<<(sym->st_info&0xf) & OK_TYPES)
     && (1<<(sym->st_info>>4) & OK_BINDS)) {
      void *symaddr = laddr(p, sym->st_value);
      if (symaddr > addr || symaddr < best)
        continue;
      best = symaddr;
      bestsym = sym;
      if (addr == symaddr)
        break;
    }
  }

  if (!best) return 0;

  if (DL_FDPIC && (bestsym->st_info&0xf) == STT_FUNC)
    best = p->funcdescs + (bestsym - p->syms);

  info->dli_fname = p->name;
  info->dli_fbase = p->base;
  info->dli_sname = strings + bestsym->st_name;
  info->dli_saddr = best;

  return 1;
}

__attribute__((__visibility__("hidden")))
void *__dlsym(void *restrict p, const char *restrict s, void *restrict ra)
{
  void *res;
  pthread_rwlock_rdlock(&lock);
  res = do_dlsym(p, s, ra);
  pthread_rwlock_unlock(&lock);
  return res;
}

int dl_iterate_phdr(int(*callback)(struct dl_phdr_info *info, size_t size, void *data), void *data)
{
  struct dso *current;
  struct dl_phdr_info info;
  int ret = 0;
  for(current = head; current;) {
    info.dlpi_addr      = (uintptr_t)current->base;
    info.dlpi_name      = current->name;
    info.dlpi_phdr      = current->phdr;
    info.dlpi_phnum     = current->phnum;
    info.dlpi_adds      = gencnt;
    info.dlpi_subs      = 0;
    info.dlpi_tls_modid = current->tls_id;
    info.dlpi_tls_data  = current->tls.image;

    ret = (callback)(&info, sizeof (info), data);

    if (ret != 0) break;

    pthread_rwlock_rdlock(&lock);
    current = current->next;
    pthread_rwlock_unlock(&lock);
  }
  return ret;
}

__attribute__((__visibility__("hidden")))
void __dl_vseterr(const char *, va_list);

static void error(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  if (!runtime) {
    vdprintf(2, fmt, ap);
    dprintf(2, "\n");
    ldso_fail = 1;
    va_end(ap);
    return;
  }
  __dl_vseterr(fmt, ap);
  va_end(ap);
}

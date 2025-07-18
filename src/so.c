#include "so.h"
#include "so-heap.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <rphii/err.h>

static void so_resize_known(So *s, size_t len_old, size_t len_new);
static size_t _so_len_known(So *s, bool is_stack);
static char *_so_it0_known(So *s, bool is_stack);

static char *so_grow_by_stack(So *so, size_t len_add);
static char *so_grow_by_heap(So *so, size_t len_add);
static char *so_grow_by_ref(So *so, size_t len_add);
static char *so_grow_by(So *so, size_t len_add);
static bool _so_is_stack(So *s);
static bool _so_is_heap(So *s);

static char *so_grow_by_stack(So *so, size_t len_add) {
    size_t len_was = so->stack.len;
    if(len_was + len_add <= SO_STACK_CAP) {
        so->stack.len += len_add;
        return so->stack.str + len_was;
    } else {
        So_Heap *heap = so_heap_grow(0, len_was + len_add);
        memcpy(heap->str, so->stack.str, len_was);
        so->ref.str = heap->str;
        so->ref.len = (len_was + len_add) | SO_HEAP_BIT;
        return so->ref.str + len_was;
    }
}

static char *so_grow_by_heap(So *so, size_t len_add) {
    size_t len_was = so->ref.len & ~SO_HEAP_BIT;
    So_Heap *heap = so_heap_base(so);
    if(len_was + len_add >= heap->cap) {
        heap = so_heap_grow(heap, len_was + len_add);
        so->ref.str = heap->str;
    }
    so->ref.len = (len_was + len_add) | SO_HEAP_BIT;
    return so->ref.str + len_was;
}

static char *so_grow_by_ref(So *so, size_t len_add) {
    size_t len_was = so->ref.len;
    if(len_was + len_add <= SO_STACK_CAP) {
        memmove(so->stack.str, so->ref.str, len_was);
        so->stack.len = len_was + len_add;
        return so->stack.str + len_was;
    } else {
        So_Heap *heap = so_heap_grow(0, len_was + len_add);
        memcpy(heap->str, so->ref.str, len_was);
        so->ref.str = heap->str;
        so->ref.len = (len_was + len_add) | SO_HEAP_BIT;
        return so->ref.str + len_was;
    }
}

static char *so_grow_by(So *so, size_t len_add) {
    if(_so_is_stack(so)) {
        return so_grow_by_stack(so, len_add);
    } else if(_so_is_heap(so)) {
        return so_grow_by_heap(so, len_add);
    } else {
        return so_grow_by_ref(so, len_add);
    }
}

bool so_is_empty(So s) {
    return !so_len(s);
}

bool so_is_stack(So s) {
    return (s.stack.len & ~SO_STACK_HEAP_BIT);
}

static bool _so_is_stack(So *s) {
    return (s->stack.len & ~SO_STACK_HEAP_BIT);
}

bool so_is_heap(So s) {
    return (s.ref.len & SO_HEAP_BIT);
}

static bool _so_is_heap(So *s) {
    return (s->ref.len & SO_HEAP_BIT);
}

size_t so_len(So s) {
    if(so_is_stack(s)) return s.stack.len;
    return s.ref.len & ~SO_HEAP_BIT;
}

size_t _so_len(So *s) {
    if(_so_is_stack(s)) return s->stack.len;
    return s->ref.len & ~SO_HEAP_BIT;
}

static inline size_t _so_len_known(So *s, bool is_stack) {
    if(is_stack) return s->stack.len;
    return s->ref.len & ~SO_HEAP_BIT;
}

void so_set_len(So *so, size_t len) {
    if(_so_is_stack(so)) {
        so->stack.len = len;
    } else if(_so_is_heap(so)) {
        so->ref.len = len | SO_HEAP_BIT;
    } else {
        so->ref.len = len;
    }
}

void so_change_len(So *so, size_t len) {
    if(_so_is_stack(so)) {
        so->stack.len += len;
    } else if(_so_is_heap(so)) {
        so->ref.len += len;
        so->ref.len |= SO_HEAP_BIT;
    } else {
        so->ref.len += len;
    }
}


void so_copy(So *s, So b) {
    so_clear(s);
    size_t len = so_len(b);
    so_resize_known(s, 0, len);
    memcpy(_so_it0(s), so_it0(b), len);
}

So so_clone(So b) {
    So result = {0};
    size_t len = so_len(b);
    so_resize_known(&result, 0, len);
    memcpy(so_it0(result), so_it0(b), len);
    return result;
}

char *so_dup(So so) {
    So_Ref ref = so_ref(so);
    char *result = malloc(ref.len + 1);
    memcpy(result, ref.str, ref.len);
    result[ref.len] = 0;
    return result;
}

void so_push(So *s, char c) {
    *so_grow_by(s, 1) = c;
}

void so_extend(So *s, So b) {
    So_Ref ref = so_ref(b);
    memcpy(so_grow_by(s, ref.len), ref.str, ref.len);
}

void so_resize_known(So *s, size_t len_old, size_t len_new) {
    bool is_stack = so_is_stack(*s);
    bool is_heap = so_is_heap(*s);
    if(len_new > len_old) {
        if(len_new >= SO_HEAP_MAX) exit(1);
        if(len_new <= SO_STACK_CAP && !is_heap) {
            if(!is_stack) {
                So_Stack stack;
                memcpy(stack.str, s->ref.str, len_old);
                s->stack = stack;
            }
            s->stack.len = len_new;
        } else {
            So_Heap *heap = is_heap ? so_heap_base(s) : 0;
            heap = so_heap_grow(heap, len_new);
            if(is_stack) memcpy(heap->str, s->stack.str, len_old);
            else if(!is_heap) memcpy(heap->str, s->ref.str, len_old);
            s->ref.str = heap->str;
            s->ref.len = len_new | SO_HEAP_BIT;
        }
    } else if(len_new < len_old) {
        if(is_stack) s->stack.len = len_new;
        else s->ref.len = len_new | (is_heap ? SO_HEAP_BIT : 0);
    }
}

void so_resize(So *s, size_t len_new) {
    bool is_stack = so_is_stack(*s);
    bool is_heap = so_is_heap(*s);
    size_t len_old = so_len(*s);
    so_resize_known(s, len_old, len_new);
}

void so_fmt(So *s, const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    so_fmt_va(s, fmt, va);
    va_end(va);
}

void so_fmt_va(So *s, const char *fmt, va_list va) {
    va_list argp2;
    va_copy(argp2, va);
    size_t len_app = (size_t)vsnprintf(0, 0, fmt, argp2);
    va_end(argp2);

#if 0
    if((int)len_app < 0) {
        ABORT("len_app is < 0!");
    }
#endif

    /* calculate required memory */
    size_t len_old = so_len(*s);
    size_t len_new = len_old + len_app;
    so_resize(s, len_new);

    /* actual append */
    int len_chng = vsnprintf(_so_it(s, len_old), len_app + 1, fmt, va);

#if 0
    // check for success
    if(!(len_chng >= 0 && (size_t)len_chng <= len_app)) {
        ABORT("len_chng is < 0!");
    }
#endif
}

const char so_at(So s, size_t i) {
    if(so_is_stack(s)) {
        return s.stack.str[i];
    } else {
        return s.ref.str[i];
    }
}

const char _so_at(So *s, size_t i) {
    if(_so_is_stack(s)) {
        return s->stack.str[i];
    } else {
        return s->ref.str[i];
    }
}

const char so_at0(So s) {
    if(so_is_stack(s)) {
        return s.stack.str[0];
    } else {
        return s.ref.str[0];
    }
}

const char so_p_at0(So *s) {
    if(_so_is_stack(s)) {
        return s->stack.str[0];
    } else {
        return s->ref.str[0];
    }
}

char *_so_it(So *s, size_t i) {
    if(_so_is_stack(s)) {
      return &s->stack.str[i];
    } else {
      return &s->ref.str[i];
    }
}

char *_so_it0(So *s) {
    return _so_it0_known(s, so_is_stack(*s));
}

static inline char *_so_it0_known(So *s, bool is_stack) {
    if(is_stack) {
      return s->stack.str;
    } else {
      return s->ref.str;
    }
}

So_Ref _so_ref(So *s) {
    bool is_stack = _so_is_stack(s);
    So_Ref result = {
        .len = _so_len_known(s, is_stack),
        .str = _so_it0_known(s, is_stack),
    };
    return result;
}

void so_clear(So *s) {
    s->ref.len &= SO_HEAP_BIT;
}

void so_free(So *s) {
    if(so_is_heap(*s)) free(so_heap_base(s));
    memset(s, 0, sizeof(*s));
}


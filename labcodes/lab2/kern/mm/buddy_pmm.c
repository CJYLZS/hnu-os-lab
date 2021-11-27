#include <pmm.h>
#include <list.h>
#include <string.h>
/*
buddy system中的内存布局
低地址                              高地址
+--------------------------------------+
| |  |    |        |                   |
+--------------------------------------+
低地址的内存块较小             高地址的内存块较大
*/
extern const struct pmm_manager default_pmm_manager;
extern free_area_t free_area;
#define free_list (free_area.free_list)
#define nr_free (free_area.nr_free)
size_t getLessNearOfPower2(size_t x) 
{
    size_t _i;
    for(_i = 0; _i < sizeof(size_t) * 8 - 1; _i++)
        if((1 << (_i+1)) > x)
            break;
    return (size_t)(1 << _i);
}
static void
buddy_init(void) {
    list_init(&free_list);
    nr_free = 0;
}
static void
buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    
    struct Page *p = base;
    for (; p != base + n; p ++) {
        assert(PageReserved(p));
        p->flags = p->property = 0;
        set_page_ref(p, 0);
    }
    nr_free += n;
    base += n;
    /*
  free_area.free_list中的内存块顺序:
  1. 一大块连续物理内存被切割后，free_area.free_list中的内存块顺序
      addr: 0x34       0x38           0x40
          +----+     +--------+     +---------------+
      <-> | 0x4| <-> | 0x8    | <-> |     0x10      | <->
          +----+     +--------+     +---------------+
  2. 几大块物理内存（这几块之间可能不连续）被切割后，free_area.free_list中的内存块顺序
      addr: 0x34       0x104       0x38           0x108          0x40                 0x110
          +----+     +----+     +--------+     +--------+     +---------------+     +---------------+
      <-> | 0x4| <-> | 0x4| <-> | 0x8    | <-> | 0x8    | <-> |     0x10      | <-> |     0x10      | <->
          +----+     +----+     +--------+     +--------+     +---------------+     +---------------+
  */
    while(n != 0)
    {
        size_t curr_n = getLessNearOfPower2(n);
        base -= curr_n;
        base->property = curr_n;
        SetPageProperty(base);
        list_entry_t* le;
        for(le = list_next(&free_list); le != &free_list; le = list_next(le))
        {
            struct Page *p = le2page(le, page_link);
            if((p->property > base->property)
                 || (p->property ==  base->property && p > base))
                break;
        }
        list_add_before(le, &(base->page_link));
        n -= curr_n;
    }
}
static struct Page *
buddy_alloc_pages(size_t n) {
    assert(n > 0);
    size_t lessOfPower2 = getLessNearOfPower2(n);
    if (lessOfPower2 < n)
        n = 2 * lessOfPower2;
    if (n > nr_free) {
        return NULL;
    }
    // 寻找合适的内存块
    struct Page *page = NULL;
    list_entry_t *le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        if (p->property >= n) {
            page = p;
            break;
        }
    }
    if (page != NULL) {
      // 进行大小切割    
        while(page->property > n)
        {
            page->property /= 2;
            
            struct Page *p = page + page->property;
            p->property = page->property;
            SetPageProperty(p);
            list_add_after(&(page->page_link), &(p->page_link));
        }
        nr_free -= n;
        ClearPageProperty(page);
        assert(page->property == n);
        list_del(&(page->page_link));
    }
    return page;
}
static void
buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    size_t lessOfPower2 = getLessNearOfPower2(n);
    if (lessOfPower2 < n)
        n = 2 * lessOfPower2;
    struct Page *p = base;
    for (; p != base + n; p ++) {
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
        set_page_ref(p, 0);
    }
    base->property = n;
    SetPageProperty(base);
    nr_free += n;
    list_entry_t *le;
    // 插入链表
    for(le = list_next(&free_list); le != &free_list; le = list_next(le))
    {
        p = le2page(le, page_link);
        if ((base->property < p->property)
                 || (p->property ==  base->property && p > base)) {
            break;
        }
    }
    list_add_before(le, &(base->page_link));
    // 向左合并
    if(base->property == p->property && p + p->property == base) {
        p->property += base->property;
        ClearPageProperty(base);
        list_del(&(base->page_link));
        base = p;
        le = &(base->page_link);
    }
    // 向右合并
    while (le != &free_list) {
        p = le2page(le, page_link);
        if (base->property == p->property && base + base->property == p)
        {
            base->property += p->property;
            ClearPageProperty(p);
            list_del(&(p->page_link));
            le = &(base->page_link);
        }
        else if(base->property < p->property)
        {
            list_entry_t* targetLe = list_next(&base->page_link);
            while(le2page(targetLe, page_link)->property < base->property)
                targetLe = list_next(targetLe);
            if(targetLe != list_next(&base->page_link))
            {
                list_del(&(base->page_link));
                list_add_before(targetLe, &(base->page_link));
            }
            break;
        }
        le = list_next(le);
    }
}
static void
basic_check(void) {
    struct Page *p0, *p1, *p2;
    p0 = p1 = p2 = NULL;
    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);
    assert(p0 != p1 && p0 != p2 && p1 != p2);
    assert(page_ref(p0) == 0 && page_ref(p1) == 0 && page_ref(p2) == 0);
    assert(page2pa(p0) < npage * PGSIZE);
    assert(page2pa(p1) < npage * PGSIZE);
    assert(page2pa(p2) < npage * PGSIZE);
    list_entry_t free_list_store = free_list;
    list_init(&free_list);
    assert(list_empty(&free_list));
    unsigned int nr_free_store = nr_free;
    nr_free = 0;
    assert(alloc_page() == NULL);
    free_page(p0);
    free_page(p1);
    free_page(p2);
    assert(nr_free == 3);
    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);
    assert(alloc_page() == NULL);
    free_page(p0);
    assert(!list_empty(&free_list));
    struct Page *p;
    assert((p = alloc_page()) == p0);
    assert(alloc_page() == NULL);
    assert(nr_free == 0);
    free_list = free_list_store;
    nr_free = nr_free_store;
    free_page(p);
    free_page(p1);
    free_page(p2);
}
static void
buddy_check(void) {
    int count = 0, total = 0;
    list_entry_t *le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        assert(PageProperty(p));
        count ++, total += p->property;
    }
    assert(total == nr_free_pages());
    basic_check();
    
    struct Page *p0 = alloc_pages(26), *p1;
    assert(p0 != NULL);
    assert(!PageProperty(p0));
    list_entry_t free_list_store = free_list;
    list_init(&free_list);
    assert(list_empty(&free_list));
    assert(alloc_page() == NULL);
    unsigned int nr_free_store = nr_free;
    nr_free = 0;
    
    free_pages(p0, 26);     
    
    p0 = alloc_pages(6);    
    p1 = alloc_pages(10);   
    assert((p0 + 8)->property == 8);
    free_pages(p1, 10);     
    assert((p0 + 8)->property == 8);
    assert(p1->property == 16);
    p1 = alloc_pages(16);   
    
    free_pages(p0, 6);      
    assert(p0->property == 16);
    free_pages(p1, 16);     
    assert(p0->property == 32);
    p0 = alloc_pages(8);    
    p1 = alloc_pages(9);    
    free_pages(p1, 9);     
    assert(p1->property == 16);
    assert((p0 + 8)->property == 8);
    free_pages(p0, 8);      
    assert(p0->property == 32);
    
    p0 = alloc_pages(5);
    p1 = alloc_pages(16);
    free_pages(p1, 16);
    assert(list_next(&(free_list)) == &((p1 - 8)->page_link));
    free_pages(p0, 5);
    assert(list_next(&(free_list)) == &(p0->page_link));
    p0 = alloc_pages(5);
    p1 = alloc_pages(16);
    free_pages(p0, 5);
    assert(list_next(&(free_list)) == &(p0->page_link));
    free_pages(p1, 16);
    assert(list_next(&(free_list)) == &(p0->page_link));
    
    p0 = alloc_pages(26);
    
    assert(nr_free == 0);
    nr_free = nr_free_store;
    free_list = free_list_store;
    free_pages(p0, 26);
    le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        assert(le->next->prev == le && le->prev->next == le);
        struct Page *p = le2page(le, page_link);
        count --, total -= p->property;
    }
}
static size_t
buddy_nr_free_pages(void) {
    return nr_free;
}
const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};

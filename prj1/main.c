#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bitmap.h"
#include "debug.h"
#include "hash.h"
#include "hex_dump.h"
#include "limits.h"
#include "list.h"
#include "round.h"

int parse_command (char *command, char **command_ptr);
void execute_command (int argc, char **argv);
int parse_num (int option, char *buf); // option 0: list, hash / option 1: bitmap

#define MAX_SIZE 10 // max size of each data structures

typedef void (*command_func) (int, char **);

struct command_entry {
  const char *name;
  command_func func;
};

struct list list[MAX_SIZE];
struct hash hash[MAX_SIZE];
struct bitmap* bm[MAX_SIZE];

list_less_func *list_less_ptr = list_less;
hash_hash_func *hash_ptr = hash_hash;
hash_less_func *hash_less_ptr = hash_less;

/* helper functions: dynamic allocation of list_item and hash_elem */
static struct list_item *
create_list_item (int data)
{
  struct list_item *item = (struct list_item *) malloc (sizeof (struct list_item));
  if (!item)
    {
      fprintf (stderr, "[ERROR] create_list_item: malloc failed\n");
      return NULL;
    }
  item->data = data;
  return item;
}

static struct hash_elem *
create_hash_elem (int data)
{
  struct hash_elem *e = (struct hash_elem *) malloc (sizeof (struct hash_elem));
  if (!e) 
    {
      fprintf (stderr, "[ERROR] create_hash_elem: malloc failed\n");
      return NULL;
    }
  e->data = data;
  return e;
}

int 
main()
{
  // read one command line in STDIN
  char command[100];
  char *command_ptr[10];
  int cnt = 0; // command counter

  while(1)
    {
      if (!fgets (command, 100, stdin))
        exit(1);

      cnt = parse_command (command, command_ptr);
      if (cnt == 0) continue; // space (no command)

      /* command: quit */
      if (!strcmp (command_ptr[0], "quit")) break;

      execute_command (cnt, command_ptr);
    }

  return 0;
}

/* Executing certain comands functions */

/* command: create */
void 
cmd_create (int argc, char **argv)
{
  /*
    e.g. create list list0 / create hashtable hash0 / create bitmap bm0 16
  */
  if (argc < 3)
    {
      fprintf (stderr, "[ERROR] create: too few args\n");
      return;
    }
  if (!strcmp (argv[1], "list"))
    {
      int idx = parse_num (0, argv[2]);
      if (idx < 0 || idx >= MAX_SIZE)
        {
          fprintf (stderr, "[ERROR] create list: invalid index\n");
          return;
        }
        list_init (&list[idx]);
    }
  else if (!strcmp (argv[1], "hashtable"))
    {
      int idx = parse_num (0, argv[2]);
      if (idx < 0 || idx >= MAX_SIZE)
        {
          fprintf (stderr, "[ERROR] create hashtable: invalid index\n");
          return;
        }
        hash_init (&hash[idx], hash_ptr, hash_less_ptr, NULL);
    }
  else if (!strcmp (argv[1], "bitmap"))
    {
      int idx = parse_num (1, argv[2]);
      if (idx < 0 || idx >= MAX_SIZE)
        {
          fprintf (stderr, "[ERROR] create bitmap: invalid index\n");
          return;
        }
      size_t bm_size = (size_t) atoi (argv[3]);
      bm[idx] = bitmap_create (bm_size);
      if (!bm[idx])
        {
          fprintf (stderr, "[ERROR] bitmap_create failed\n");
          exit(1);
        }
    }
  else
    {
      fprintf (stderr, "[ERROR] create: unknown type \'%s\'\n", argv[1]);  
    }
}

/* command: dumpdata */
void
cmd_dumpdata (int argc, char **argv)
{
  /*
    e.g. dumpdata list0 / dumpdata hash0 / dumpdata bm0
  */
  if (argc < 2)
    {
      fprintf (stderr, "[ERROR] dumpdata: too few args\n");
      return;
    }
  if (!strncmp (argv[1], "list", 4))
    {
      int idx = parse_num (0, argv[1]);
      list_print (&list[idx]);
    }
  else if (!strncmp (argv[1], "hash", 4))
    {
      int idx = parse_num (0, argv[1]);
      hash_print (&hash[idx]);
    }
  else if (!strncmp (argv[1], "bm", 2))
    {
      int idx = parse_num (1, argv[1]);
      bitmap_print (bm[idx]);
    }
  else
    {
      fprintf (stderr, "[ERROR] dumpdata: unknown type \'%s\'\n", argv[1]);
    }
}

/* command: delete */
void
cmd_delete (int argc, char **argv)
{
  /*
    e.g. delete list0 / delete hash0 / delete bm0
  */
  if (argc < 2)
    {
      fprintf (stderr, "[ERROR] delete: too few args\n");
      return;
    } 
  if (!strncmp (argv[1], "list", 4))
    {
      int idx = parse_num (0, argv[1]);
      list_delete (&list[idx]);
    }
    else if (!strncmp (argv[1], "hash", 4))
      {
        int idx = parse_num (0, argv[1]);
        hash_clear (&hash[idx], hash_free);
      }
    else if (!strncmp (argv[1], "bm", 2))
      {
        int idx = parse_num (1, argv[1]);
        bitmap_destroy (bm[idx]);
      }
    else 
      {
        fprintf (stderr, "[ERROR] delete: unknown type \'%s\'\n", argv[1]);
      } 
}

/* LIST commands */

/* command: list_push_back */
void
cmd_list_push_back (int argc, char **argv)
{
  /*
    e.g. list_push_back list0 1
  */
  if (argc < 3)
    {
      fprintf (stderr, "[ERROR] list_push_back: too few args\n");
      return;
    }
  int idx = parse_num (0, argv[1]);
  int val = atoi (argv[2]);
  struct list_item *temp = create_list_item (val);
  if (!temp) return;
  list_push_back (&list[idx], &temp->elem);
}

/* command: list_insert */
void
cmd_list_insert (int argc, char **argv)
{
  /*
    e.g. list_insert list0 0 1
  */
 if (argc < 4)
  {
    fprintf(stderr, "[ERROR] list_insert: too few args\n");
    return;
  }
  int idx = parse_num (0, argv[1]);
  int pos = atoi (argv[2]);
  int val = atoi (argv[3]);

  struct list_item *temp = create_list_item (val);
  if (!temp) return;
  list_insert (list_access (&list[idx], pos), &(temp->elem));
}

/* command: list_swap */
void
cmd_list_swap (int argc, char **argv)
{
  /*
    e.g. list_swap list0 1 3
  */
  if (argc < 4)
    {
      fprintf (stderr, "[ERROR] list_swap: too few args\n");
      return;
    }
  int idx = parse_num (0, argv[1]);
  int a = atoi (argv[2]);
  int b = atoi (argv[3]);
  list_swap (list_access (&list[idx], a), list_access (&list[idx], b));
}

/* command: list_splice */
void
cmd_list_splice (int argc, char **argv)
{
  /*
    e.g. list_splice list0 2 list1 1 4
  */
  if (argc < 6)
    {
      fprintf(stderr, "[ERROR] list_splice: too few args\n");
      return;
    }
  int idx1 = parse_num (0, argv[1]);
  int pos = atoi (argv[2]);
  struct list_elem *e = list_access (&list[idx1], pos);

  int idx2 = parse_num (0, argv[3]);
  int start = atoi (argv[4]);
  int end = atoi (argv[5]);

  struct list_elem *first = list_access (&list[idx2], start);
  struct list_elem *last = list_access (&list[idx2], end);
  list_splice (e, first, last);
}

/* command: list_sort */
void
cmd_list_sort (int argc, char **argv)
{
  /*
    e.g. list_sort list0
  */
  if (argc < 2)
    {
      fprintf(stderr, "[ERROR] list_sort: too few args\n");
      return;
    }
  int idx = parse_num(0, argv[1]);
  list_sort(&list[idx], list_less_ptr, NULL);
}

/* command: list_unique */
void
cmd_list_unique (int argc, char **argv)
{
  /*
    e.g. list_unique list0 list1 / list_unique list1
  */
  if (argc < 2)
    {
      fprintf(stderr, "[ERROR] list_unique: too few args\n");
      return;
    }
  int idx1 = parse_num (0, argv[1]);
  if (argc > 2)
    {
      // list_unique list0 list
      int idx2 = parse_num (0, argv[2]);
      list_unique (&list[idx1], &list[idx2], list_less_ptr, NULL);
    }
  else
    {
      list_unique(&list[idx1], NULL, list_less_ptr, NULL);
    }
}

/* command: list_reverse */
void
cmd_list_reverse (int argc, char **argv)
{
  /*
    e.g. list_reverse list0
  */
  if (argc < 2)
    {
      fprintf(stderr, "[ERROR] list_reverse: too few args\n");
      return;
    }
  int idx = parse_num (0, argv[1]);
  list_reverse (&list[idx]);
}

/* command: list_remove */
void
cmd_list_remove (int argc, char **argv)
{
  /*
    e.g. list_remove list0 0
  */
  if (argc < 3)
    {
      fprintf(stderr, "[ERROR] list_remove: too few args\n");
      return;
    }
  int idx = parse_num (0, argv[1]);
  int pos = atoi (argv[2]);
  struct list_elem *e = list_access (&list[idx], pos);
  list_remove(e);
}

/* command: list_front */
void
cmd_list_front (int argc, char **argv)
{
  /*
    e.g. list_front list0
  */
  if (argc < 2) 
    {
      fprintf (stderr, "[ERROR] list_front: too few args\n");
      return;
    }
  int idx = parse_num (0, argv[1]);
  struct list_item *temp = list_entry (list_front (&list[idx]), struct list_item, elem);
  printf("%d\n", temp->data);
}

/* command: list_back */
void
cmd_list_back (int argc, char **argv)
{
  /*
    e.g. list_back list0
  */
 if (argc < 2) 
    {
      fprintf (stderr, "[ERROR] list_back: too few args\n");
      return;
    }
  int idx = parse_num (0, argv[1]);
  struct list_item *temp = list_entry (list_back (&list[idx]), struct list_item, elem);
  printf("%d\n", temp->data);
}

/* command: list_push_front */
void
cmd_list_push_front (int argc, char **argv)
{
  /*
    e.g. list_push_front list0 1
  */
  if (argc < 3)
    {
      fprintf (stderr, "[ERROR] list_push_front: too few args\n");
      return;
    }
  int idx = parse_num (0, argv[1]);
  int val = atoi (argv[2]);
  struct list_item *temp = create_list_item (val);
  if (!temp) return;
  list_push_front (&list[idx], &(temp->elem));
}

/* command: list_pop_front */
void
cmd_list_pop_front (int argc, char **argv)
{
  /*
    e.g. list_pop_front list0
  */
  if (argc < 2)
    {
      fprintf (stderr, "[ERROR] list_pop_front: too few args\n");
      return;
    }
  int idx = parse_num (0, argv[1]);
  list_pop_front (&list[idx]);
}

/* command: list_pop_back */
void
cmd_list_pop_back (int argc, char **argv)
{
  /*
    e.g. list_pop_back list0
  */ 
  if (argc < 2)
    {
      fprintf (stderr, "[ERROR] list_pop_back: too few args\n");
      return;
    }
  int idx = parse_num (0, argv[1]);
  list_pop_back (&list[idx]);
}

/* command: list_empty */
void
cmd_list_empty (int argc, char **argv)
{
  /*
    e.g. list_empty list0
  */
  if (argc < 2)
    {
      fprintf (stderr, "[ERROR] list_empty: too few args\n");
      return;
    }
  int idx = parse_num (0, argv[1]);
  printf("%s\n", list_empty (&list[idx]) ? "true" : "false");
}

/* command: list_size */
void
cmd_list_size (int argc, char **argv)
{
  /*
    e.g. list_size list0
  */
  if (argc < 2)
    {
      fprintf (stderr, "[ERROR] list_size: too few args\n");
      return;
    }
  int idx = parse_num (0, argv[1]);
  printf("%zu\n", list_size (&list[idx]));
}

/* command: list_max */
void
cmd_list_max (int argc, char **argv)
{
  /*
    e.g. list_max list0
  */
  if (argc < 2)
    {
      fprintf (stderr, "[ERROR] list_max: too few args\n");
      return;
    }
  int idx = parse_num (0, argv[1]);
  struct list_item *temp = list_entry (list_max (&list[idx], list_less_ptr, NULL), struct list_item, elem);
  printf("%d\n", temp->data);
}

/* command: list_min */
void
cmd_list_min (int argc, char **argv)
{
  /*
    e.g. list_min list0
  */
  if (argc < 2)
    {
      fprintf (stderr, "[ERROR] list_min: too few args\n");
      return;
    }
  int idx = parse_num (0, argv[1]);
  struct list_item *temp = list_entry (list_min (&list[idx], list_less_ptr, NULL), struct list_item, elem);
  printf("%d\n", temp->data);
}

/* command: list_insert_ordered */
void
cmd_list_insert_ordered (int argc, char **argv)
{
  /*
    e.g. list_insert_ordered list0 5
  */
  if (argc < 3)
    {
      fprintf (stderr, "[ERROR] list_insert_ordered: too few args\n");
      return;
    }
  int idx = parse_num (0, argv[1]);
  int val = atoi (argv[2]);
  struct list_item *temp = create_list_item (val);
  if (!temp) return;

  list_sort (&list[idx], list_less_ptr, NULL);
  list_insert_ordered (&list[idx], &(temp->elem), list_less_ptr, NULL);
}

/* command: list_shuffle */
void
cmd_list_shuffle (int argc, char **argv)
{
  /*
    e.g. list_shuffle list0
  */
  if (argc < 2)
    {
      fprintf (stderr, "[ERROR] list_shuffle: too few args\n");
      return;
    }
  int idx = parse_num (0, argv[1]);
  list_shuffle (&list[idx]);
}

/* HASH commands */

/* command: hash_replace */
void
cmd_hash_replace (int argc, char **argv)
{
  /*
    e.g. hash_replace hash0 10
  */
  if (argc < 3)
    {
      fprintf(stderr, "[ERROR] hash_replace: too few args\n");
      return;
    }
  int idx = parse_num (0, argv[1]);
  int val = atoi (argv[2]);
  struct hash_elem *e = create_hash_elem (val);
  if (!e) return;
  hash_replace (&hash[idx], e);
}

/* command: hash_insert */
void
cmd_hash_insert (int argc, char **argv)
{
  /*
    e.g. hash_insert hash0 10
  */
  if (argc < 3)
    {
      fprintf(stderr, "[ERROR] hash_insert: too few args\n");
      return;
    }
  int idx = parse_num (0, argv[1]);
  int val = atoi (argv[2]);
  struct hash_elem *e = create_hash_elem (val);
  if (!e) return;
  hash_insert (&hash[idx], e);
}

/* command: hash_find */
void
cmd_hash_find (int argc, char **argv)
{
  /*
    e.g. hash_find hash0 10
  */
  if (argc < 3)
    {
      fprintf(stderr, "[ERROR] hash_find: too few args\n");
      return;
    }
  int idx = parse_num (0, argv[1]);
  int val = atoi (argv[2]);
  struct hash_elem *e = create_hash_elem (val);
  if (hash_find(&hash[idx], e))
    printf("%d\n", val);
  free (e);
}

/* command: hash_empty */
void
cmd_hash_empty (int argc, char **argv)
{
  /*
    e.g. hash_empty hash0
  */
  if (argc < 2)
    {
      fprintf(stderr, "[ERROR] hash_empty: too few args\n");
      return;
    }
  int idx = parse_num (0, argv[1]);
  printf("%s\n", hash_empty(&hash[idx]) ? "true" : "false");
}

/* command: hash_size */
void
cmd_hash_size (int argc, char **argv)
{
  /*
    e.g. hash_size hash0
  */
  if (argc < 2)
    {
      fprintf(stderr, "[ERROR] hash_size: too few args\n");
      return;
    }
  int idx = parse_num (0, argv[1]);
  printf("%zu\n", hash_size (&hash[idx]));
}

/* command: hash_clear*/
void
cmd_hash_clear (int argc, char **argv)
{
  /*
    e.g. hash_clear hash0
  */
  if (argc < 2)
    {
      fprintf(stderr, "[ERROR] hash_clear: too few args\n");
      return;
    }
  int idx = parse_num (0, argv[1]);
  hash_clear (&hash[idx], hash_free);
}

/* command: hash_delete */
void
cmd_hash_delete (int argc, char **argv)
{
  /*
    e.g. hash_delete hash0 10
  */
  if (argc < 3)
    {
      fprintf(stderr, "[ERROR] hash_delete: too few args\n");
      return;
    }
  int idx = parse_num (0, argv[1]);
  int val = atoi (argv[2]);
  struct hash_elem *e = create_hash_elem (val);
  if (!e) return;
  hash_delete (&hash[idx], e);
  free (e);
}

/* command: hash_apply*/
void
cmd_hash_apply (int argc, char **argv)
{
  /*
    e.g. hash_apply hash0 square / hash_apply hash0 triple
  */
  if (argc < 3)
    {
      fprintf (stderr, "[ERROR] hash_apply: too few args\n");
      return;
    }
  int idx = parse_num (0, argv[1]);
  hash_action_func *func = NULL;

  if (!strcmp (argv[2], "square"))
    {
      func = hash_square;
    }
  else if (!strcmp (argv[2], "triple"))
    {
      func = hash_triple;
    }
  else
    {
      fprintf (stderr, "[ERROR] hash_apply: unknown action \'%s\'\n", argv[2]);
      return;
    }
  hash_apply (&hash[idx], func);
}

/* BITMAP commands */

/* command: bitmap_mark */
void
cmd_bitmap_mark (int argc, char **argv)
{
  /*
    e.g. bitmap_mark bm0 5
  */
  if (argc < 3)
    {
      fprintf (stderr, "[ERROR] bitmap_mark: too few args\n");
      return;
    }
  int idx = parse_num (1, argv[1]);
  size_t pos = (size_t) atoi (argv[2]);
  bitmap_mark (bm[idx], pos);
}

/* command: bitmap_test */
void
cmd_bitmap_test (int argc, char **argv)
{
  /*
    e.g. bitmap_test bm0 4
  */
  if (argc < 3)
    {
      fprintf (stderr, "[ERROR] bitmap_test: too few args\n");
      return;
    }
  int idx = parse_num (1, argv[1]);
  size_t pos = (size_t) atoi (argv[2]);
  printf("%s\n", bitmap_test(bm[idx], pos) ? "true" : "false");
}

/* command: bitmap_size */
void
cmd_bitmap_size (int argc, char **argv)
{
  /*
    e.g. bitmap_size bm0
  */
  if (argc < 2)
    {
      fprintf (stderr, "[ERROR] bitmap_size: too few args\n");
      return;
    }
  int idx = parse_num (1, argv[1]);
  printf("%zu\n", bitmap_size (bm[idx]));
}

/* command: bitmap_set */
void
cmd_bitmap_set (int argc, char **argv)
{
  /*
    e.g. bitmap_set bm0 0 true
  */
  if (argc < 4)
    {
      fprintf (stderr, "[ERROR] bitmap_set: too few args\n");
      return;
    }
  int idx = parse_num (1, argv[1]);
  size_t pos = (size_t) atoi (argv[2]);
  bool val = (!strcmp (argv[3], "true")) ? true : false;
  bitmap_set (bm[idx], pos, val);
}

/* command: bitmap_set_multiple */
void
cmd_bitmap_set_multiple (int argc, char **argv)
{
  /*
    e.g. bitmap_set_multiple bm 0 4 true
  */
  if (argc < 5)
    {
      fprintf (stderr, "[ERROR] bitmap_set_multiple: too few args\n");
      return;
    }
  int idx = parse_num (1, argv[1]);
  size_t start = (size_t) atoi (argv[2]);
  size_t cnt = (size_t) atoi (argv[3]);
  bool val = (!strcmp (argv[4], "true")) ? true : false;
  bitmap_set_multiple (bm[idx], start, cnt, val);
}

/* command: bitmap_set_all */
void
cmd_bitmap_set_all (int argc, char **argv)
{
  /*
    e.g. bitmap_set_all bm0 true
  */
  if (argc < 3)
    {
      fprintf (stderr, "[ERROR] bitmap_set_all: too few args\n");
      return;
    }
  int idx = parse_num (1, argv[1]);
  bool val = (!strcmp (argv[2], "true")) ? true : false;
  bitmap_set_all (bm[idx], val);
}

/* command: bitmap_scan */
void
cmd_bitmap_scan (int argc, char **argv)
{
  /*
    e.g. bitmap_sacn bm0 0 1 true
  */
  if (argc < 5)
    {
      fprintf (stderr, "[ERROR] bitmap_scan: too few args\n");
      return;
    }
  int idx = parse_num (1, argv[1]);
  size_t start = (size_t) atoi (argv[2]);
  size_t cnt = (size_t) atoi (argv[3]);
  bool val = (!strcmp (argv[4], "true")) ? true : false;
  printf("%zu\n", bitmap_scan(bm[idx], start, cnt, val));
}

/* command: bitmap_scan_and_flip */
void
cmd_bitmap_scan_and_flip (int argc, char **argv)
{
  /*
    e.g. bitmap_scan_and_flip bm0 0 1 true
  */
  if (argc < 5)
    {
      fprintf (stderr, "[ERROR] bitmap_scan_and_flip: too few args\n");
      return;
    }
  int idx = parse_num (1, argv[1]);
  size_t start = (size_t) atoi (argv[2]);
  size_t cnt = (size_t) atoi (argv[3]);
  bool val = (!strcmp (argv[4], "true")) ? true : false;
  printf("%zu\n", bitmap_scan_and_flip(bm[idx], start, cnt, val));
}

/* command: bitmap_reset */
void
cmd_bitmap_reset (int argc, char **argv)
{
  /*
    e.g. bitmap_reset bm0 0
  */
  if (argc < 3)
    {
      fprintf(stderr, "[ERROR] bitmap_reset: too few args\n");
      return;
    }
  int idx = parse_num (1, argv[1]);
  size_t pos = (size_t) atoi (argv[2]);
  bitmap_reset (bm[idx], pos);
}

/* command: bitmap_none */
void
cmd_bitmap_none (int argc, char **argv)
{
  /*
    e.g. bitmap_none bm0 0 1
  */
  if (argc < 4)
    {
      fprintf (stderr, "[ERROR] bitmap_none: too few args\n");
      return;
    }
  int idx = parse_num (1, argv[1]);
  size_t start = (size_t) atoi (argv[2]);
  size_t cnt = (size_t) atoi (argv[3]);
  printf("%s\n", (bitmap_none (bm[idx], start, cnt) ? "true" : "false"));
}

/* command: bitmap_flip */
void
cmd_bitmap_flip (int argc, char **argv)
{
  /*
    e.g. bitmap_flip bm0 4
  */
  if (argc < 3)
    {
      fprintf (stderr, "[ERROR] bitmap_flip: too few args\n");
      return;
    }
  int idx = parse_num (1, argv[1]);
  size_t pos = (size_t) atoi (argv[2]);
  bitmap_flip (bm[idx], pos);
  return;
}

/* command: bitmap_expand */
void
cmd_bitmap_expand (int argc, char **argv)
{
  /*
    e.g. bitmap_expand bm0 2
  */
  if (argc < 3)
    {
      fprintf (stderr, "[ERROR] bitmap_expand: too few args\n");
      return;
    }
  int idx = parse_num (1, argv[1]);
  size_t size = (size_t) atoi (argv[2]);
  bm[idx] = bitmap_expand (bm[idx], size);
}

/* command: bitmap_dump */
void
cmd_bitmap_dump (int argc, char **argv)
{
  /*
    e.g. bitmap_dump bm0
  */
  if (argc < 2)
    {
      fprintf (stderr, "[ERROR] bitmap_dump: too few args\n");
      return;
    }
  int idx = parse_num (1, argv[1]);
  bitmap_dump(bm[idx]);
}

/* command: bitmap_count */
void
cmd_bitmap_count (int argc, char **argv)
{
  /*
    e.g. bitmap_count bm0 0 8 true
  */
  if (argc < 5)
    {
      fprintf (stderr, "[ERROR] bitmap_count: too few args\n");
      return;
    }
  int idx = parse_num (1, argv[1]);
  size_t start = (size_t) atoi (argv[2]);
  size_t cnt = (size_t) atoi (argv[3]);
  bool val = (!strcmp (argv[4], "true")) ? true : false;
  printf("%zu\n", bitmap_count (bm[idx], start, cnt, val));
}

/* command: bitmap_contains */
void
cmd_bitmap_contains (int argc, char **argv)
{
  /*
    e.g. bitmap_contains bm0 0 2 true
  */
  if (argc < 5)
    {
      fprintf (stderr, "[ERROR] bitmap_contains: too few args\n");
      return;
    }
  int idx = parse_num (1, argv[1]);
  size_t start = (size_t) atoi (argv[2]);
  size_t cnt  = (size_t) atoi (argv[3]);
  bool val = (!strcmp (argv[4], "true")) ? true : false;
  printf("%s\n", (bitmap_contains (bm[idx], start, cnt, val) ? "true" : "false"));
}

/* command: bitmap_any */
void
cmd_bitmap_any (int argc, char **argv)
{
  /*
    e.g. bitmap_any bm0 0 1
  */
  if (argc < 4)
    {
      fprintf (stderr, "[ERROR] bitmap_any: too few args\n");
      return;
    }
  int idx = parse_num (1, argv[1]);
  size_t start = (size_t) atoi (argv[2]);
  size_t cnt = (size_t) atoi (argv[3]);
  printf("%s\n", (bitmap_any (bm[idx], start, cnt) ? "true" : "false"));
}

/* command: bitmap_all */
void
cmd_bitmap_all (int argc, char **argv)
{
  /*
    e.g. bitmap_all bm0 0 1
  */
  if (argc < 4)
    {
      fprintf(stderr, "[ERROR] bitmap_all: too few args\n");
      return;
    }
  int idx = parse_num (1, argv[1]);
  size_t start = (size_t) atoi (argv[2]);
  size_t cnt = (size_t) atoi (argv[3]);
  printf("%s\n", (bitmap_all (bm[idx], start, cnt) ? "true" : "false"));
}

static struct command_entry cmd_table[] = {
  // create / dumpdata / delete
  {"create",              cmd_create},
  {"dumpdata",            cmd_dumpdata},
  {"delete",              cmd_delete},

  // list commands
  {"list_push_back",       cmd_list_push_back},
  {"list_insert",          cmd_list_insert},
  {"list_swap",            cmd_list_swap},
  {"list_splice",          cmd_list_splice},
  {"list_sort",            cmd_list_sort},
  {"list_unique",          cmd_list_unique},
  {"list_reverse",         cmd_list_reverse},
  {"list_remove",          cmd_list_remove},
  {"list_front",           cmd_list_front},
  {"list_back",            cmd_list_back},
  {"list_push_front",      cmd_list_push_front},
  {"list_pop_front",       cmd_list_pop_front},
  {"list_pop_back",        cmd_list_pop_back},
  {"list_empty",           cmd_list_empty},
  {"list_size",            cmd_list_size},
  {"list_max",             cmd_list_max},
  {"list_min",             cmd_list_min},
  {"list_insert_ordered",  cmd_list_insert_ordered},
  {"list_shuffle",         cmd_list_shuffle},
  
  // hashtable commands
  {"hash_replace",         cmd_hash_replace},
  {"hash_insert",          cmd_hash_insert},
  {"hash_find",            cmd_hash_find},
  {"hash_empty",           cmd_hash_empty},
  {"hash_size",            cmd_hash_size},
  {"hash_clear",           cmd_hash_clear},
  {"hash_delete",          cmd_hash_delete},
  {"hash_apply",           cmd_hash_apply},

  // bitmap commands
  {"bitmap_mark",          cmd_bitmap_mark},
  {"bitmap_test",          cmd_bitmap_test},
  {"bitmap_size",          cmd_bitmap_size},
  {"bitmap_set",           cmd_bitmap_set},
  {"bitmap_set_multiple",  cmd_bitmap_set_multiple},
  {"bitmap_set_all",       cmd_bitmap_set_all},
  {"bitmap_scan",          cmd_bitmap_scan},
  {"bitmap_scan_and_flip", cmd_bitmap_scan_and_flip},
  {"bitmap_reset",         cmd_bitmap_reset},
  {"bitmap_none",          cmd_bitmap_none},
  {"bitmap_flip",          cmd_bitmap_flip},
  {"bitmap_expand",        cmd_bitmap_expand},
  {"bitmap_dump",          cmd_bitmap_dump},
  {"bitmap_count",         cmd_bitmap_count},
  {"bitmap_contains",      cmd_bitmap_contains},
  {"bitmap_any",           cmd_bitmap_any},
  {"bitmap_all",           cmd_bitmap_all},
};

/* Return the number of words in command (seperated by " ") */
int
parse_command (char *command, char **command_ptr)
{
  int word_cnt = 0;
  int i = 0;
  char *buf;

  if (buf = strchr (command, '\n'))
    *buf = '\0'; // convert '\n' to '\0'
  
  buf = strtok (command, " ");
  while (buf != NULL)
    {
      command_ptr[i++] = buf;
      buf = strtok (NULL, " ");
      word_cnt++;
    }

  return word_cnt;
}

/* Execute the command */
void
execute_command (int argc, char **argv)
{       
  for (size_t i = 0; i < sizeof (cmd_table) / sizeof (cmd_table[0]); i++)
    {
      if (!strcmp (argv[0], cmd_table[i].name))
        {
          cmd_table[i].func(argc, argv);
          return;
        }
    }
    fprintf (stderr, "[ERROR] Unknow command: %s\n", argv[0]);
}

/* Returns the the number of data structure 
   e.g create list list3 -> parse_num(0, command_ptr[2]) returns 3
*/
int
parse_num (int option, char *buf)
{
  ASSERT (buf != NULL);
  // listX, hashX -> atoi (buf + 4), bmX -> atoi (buf + 2)
  return atoi (buf + (option == 0 ? 4 : 2));
}
/*
 * stockserver.c - A level-order binary tree based stock server
 *
 * server ip: 172.30.10.11 (cspro)
 * port: 60029
 */
#include "csapp.h"
#include <time.h>

static struct timespec first_connect = {0}, last_disconnect = {0};

#define NTHREADS 20
#define MAX_STOCK_NUM 1024
#define SBUFSIZE 1024

/* thread routine */
void *thread(void *vargp);

static int clientcnt = 0;
sem_t f; /* semaphore for clientcnt */

/* sbuf_t: Bounded buffer used by the SBUF package */
typedef struct {
    int *buf;               /* Buffer array */
    int n;                  /* Maximum number of slots */
    int front;              /* buf[(front+1)%n] is first item */
    int rear;               /* buf[rear%n] is last item */
    sem_t mutex;            /* Protects accesses to buf */
    sem_t slots;            /* Counts available slots */
    sem_t items;            /* Counts available items */
} sbuf_t;

sbuf_t sbuf; /* Shared buffer of connected descriptors */

/* sbuf functions for synchronizing concurrent access to bounded buffers */
static void sbuf_init(sbuf_t *sp, int n);
static void sbuf_deinit(sbuf_t *sp);
static void sbuf_insert(sbuf_t *sp, int item);
static int sbuf_remove(sbuf_t *sp);

/* Stock databse encapsulation */
typedef struct node {
    int ID;
    int left_stock;
    int price;
    struct node *left, *right;
    sem_t mutex; /* write 위한 semaphore */
} node_t;

typedef struct {
    node_t *root;
    char buf[MAXLINE];
} stockdb_t;

static stockdb_t db;

/* signal handler */
static void sigint_handler(int sig);

/* stock operations */
static void load_stock(const char *path);
static void dump_stock(const char *path);
static size_t list_stock(void);
static int change_stock(int id, char req, int amt);
static void free_stockdb(void);

/* binary tree operations */
static node_t *node_create(int id, int stock, int price);
static void node_insert(node_t **proot, node_t *new_node);
static node_t *node_search(node_t *r, int id);
static void node_free(node_t *r);


/* server pool operations */
static void handle_request(int connfd);

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }

    Signal(SIGINT, sigint_handler);
    load_stock("stock.txt"); /* load stock data from file to memory */

    int i, listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    char client_host[MAXLINE], client_port[MAXLINE];
    pthread_t tid;
    
    listenfd = Open_listenfd(argv[1]);

    sbuf_init(&sbuf, SBUFSIZE);
    for (i = 0; i < NTHREADS; i++) { /* Create worker threads */
        Pthread_create(&tid, NULL, thread, NULL);
    }

    Sem_init(&f, 0, 1); /* initialize f semaphore */

    while (1) {
        clientlen = sizeof(struct sockaddr_storage);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

        P(&f);
        clientcnt++;
        V(&f);

        if (clientcnt == 1 && first_connect.tv_sec == 0) {
            /* first client just arrived */
            clock_gettime(CLOCK_MONOTONIC, &first_connect);
            printf("start timer!!\n");
        }
        
        Getnameinfo((SA*)&clientaddr, clientlen, client_host, MAXLINE, client_port, MAXLINE, 0);
        printf("Conncected to (%s, %s)\n", client_host, client_port);
        sbuf_insert(&sbuf, connfd); /* Insert connfd in buffer */
    }

    return 0;
}

/* thread routing function */
void *thread(void *vargp) {
    Pthread_detach(pthread_self());
    while(1) {
        int connfd = sbuf_remove(&sbuf); /* Remove connfd from buffer */
        handle_request(connfd); /* Service client */
        Close(connfd);

        clock_gettime(CLOCK_MONOTONIC, &last_disconnect);

        P(&f);
        clientcnt--;
        V(&f);

        if(clientcnt == 0) { /* 연결된 client 없으면 메모리에 있는 주식 정보를 파일에 기록*/
            printf("no client!!\n");
            if (first_connect.tv_sec != 0) {
                double elapsed = (last_disconnect.tv_sec - first_connect.tv_sec) + (last_disconnect.tv_nsec - first_connect.tv_nsec) / 1e9;
                printf(">> elapsed time: %.3f\n", elapsed);
            }
            dump_stock("stock.txt");
        }
    }
}

/*-------------- sbuf manipulating functions --------------*/
/* Create an empty, bounded, shared FIFO buffer with n slots */
static void sbuf_init(sbuf_t *sp, int n) {
    sp->buf = Calloc(n, sizeof(int));
    sp->n = n;                          /* Buffer holds max of n items */
    sp->front = sp->rear = 0;           /* Empty buffer iff front == rear */
    Sem_init(&sp->mutex, 0, 1);         /* Binary semaphore for locking */
    Sem_init(&sp->slots, 0, n);         /* Initially, buf has n empty slots */
    Sem_init(&sp->items, 0, 0);         /* Initially, buf has zero data items */
}

/* Clean up buffer sp */
static void sbuf_deinit(sbuf_t *sp) {
    Free(sp->buf);
}

/* Insert item onto the rear of shared buffer sp */
static void sbuf_insert(sbuf_t *sp, int item) {
    P(&sp->slots);                              /* Wait for available slot */
    P(&sp->mutex);                              /* Lock the buffer */
    sp->buf[(++sp->rear)%(sp->n)] = item;       /* Insert the item */
    V(&sp->mutex);                              /* Unlock the buffer */    
    V(&sp->items);                              /* Announce available item */
}

/* Remove and return the first item from buffer sp */
static int sbuf_remove(sbuf_t *sp) {
    int item;
    P(&sp->items);                                  /* Wait for available item*/
    P(&sp->mutex);                                  /* Lock the buffer */
    item = sp->buf[(++sp->front)%(sp->n)];   /* Remove the item */
    V(&sp->mutex);                                  /* Unlock the buffer */
    V(&sp->slots);                                  /* Announce availabe slot */
    return item;
}

/*-------------- Signal handler --------------*/
/* SIGINT signal handler */
static void sigint_handler(int sig) {
    dump_stock("stock.txt");
    free_stockdb();
    exit(0);
}

/*-------------- Stock DB operations --------------*/
/* Load stock data from a file */
static void load_stock(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { 
        perror("fopen");
        exit(1); 
    }
    int id, st, pr;
    while (fscanf(f, "%d %d %d", &id, &st, &pr) == 3) {
        node_t *n = node_create(id, st, pr);
        node_insert(&db.root, n);
    }
    fclose(f);
}

/* Dump stock data to a file */
static void dump_stock(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) {
        perror("fopen");
        exit(1); 
    }
    if (!db.root) { fclose(f); return; }
    node_t *queue[MAX_STOCK_NUM];
    int head = 0, tail = 0;
    queue[tail++] = db.root;
    while (head < tail) {
        node_t *cur = queue[head++];
        fprintf(f, "%d %d %d\n", cur->ID, cur->left_stock, cur->price);
        if (cur->left)  queue[tail++] = cur->left;
        if (cur->right) queue[tail++] = cur->right;
    }
    fclose(f);
}

/* List all stocks */
static size_t list_stock(void) {
    size_t off = 0;
    db.buf[0] = '\0';
    if (!db.root) return 0;
    node_t *queue[MAX_STOCK_NUM];
    int head = 0, tail = 0;
    queue[tail++] = db.root;
    while (head < tail) {
        node_t *cur = queue[head++];
        int len = snprintf(db.buf + off, MAXLINE - off,
                           "%d %d %d\n",
                           cur->ID, cur->left_stock, cur->price);
        if (len < 0 || (size_t)len >= MAXLINE - off) break; /* snprintf error or bufferoverflow */
        off += len;
        if (cur->left)  queue[tail++] = cur->left;
        if (cur->right) queue[tail++] = cur->right;
    }
    return off;
}

/* Change stock */
static int change_stock(int id, char req, int amt) {
    node_t *n = node_search(db.root, id);
    if (!n) return -1; /* Invalid ID */

    P(&(n->mutex));
    if (req == 'b') {
        if (n->left_stock < amt) return 1; /* Not enough stock*/        
        n->left_stock -= amt;
    } else if (req == 's') {
        n->left_stock += amt;
    }
    V(&(n->mutex));

    return 0; /* Success */
}

/* Free the stock database */
static void free_stockdb(void) {
    node_free(db.root);
    db.root = NULL;
}

/*-------------- Binary Tree operations --------------*/
/* Create a new node */
static node_t *node_create(int id, int stock, int price) {
    node_t *n = malloc(sizeof(*n));
    if (!n) { 
        perror("malloc"); 
        exit(1); 
    }
    n->ID = id;
    n->left_stock = stock;
    n->price = price;
    n->left = n->right = NULL;
    Sem_init(&n->mutex, 0, 1);
    return n;
}

/* Insert a new node into the binary tree (level order insertion) */
static void node_insert(node_t **proot, node_t *new_node) {
    if (!*proot) { /* If the tree is empty */
        *proot = new_node;
        return;
    }
    node_t *queue[MAX_STOCK_NUM];
    int head = 0, tail = 0;
    queue[tail++] = *proot;
    while (head < tail) {
        node_t *cur = queue[head++];
        if (!cur->left) {
            cur->left = new_node;
            return;
        }
        queue[tail++] = cur->left;
        if (!cur->right) {
            cur->right = new_node;
            return;
        }
        queue[tail++] = cur->right;
    }
}

/* Search for a node by ID in the binary tree */
static node_t *node_search(node_t *r, int id) {
    if (!r) return NULL;
    /* fixed-size queue */
    node_t *queue[MAX_STOCK_NUM];
    int head = 0, tail = 0;

    queue[tail++] = r;

    while(head < tail) {
        node_t *cur = queue[head++];

        if(cur->ID == id) return cur;

        if(cur->left) queue[tail++] = cur->left;
        if(cur->right) queue[tail++] = cur->right;
    }

    return NULL; /* Not found */
}

/* Free the binary tree recursively*/
static void node_free(node_t *r) {
    if (!r) return;
    node_free(r->left);
    node_free(r->right);
    free(r);
}

/* Handle a clients' request */
static void handle_request(int connfd) {
    int n;
    char buf[MAXLINE];
    char response[MAXLINE];
    const char delim[] = " ";
    rio_t rio;

    Rio_readinitb(&rio, connfd); /* Initialize connfd's rio */

    while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {
        printf("server received %d bytes\n", n);

        response[0] = '\0';

        char *token = strtok(buf, delim);

        if (!strcmp(token, "show\n")) {
            size_t n = list_stock();
            Rio_writen(connfd, db.buf, MAXLINE);
        } else if (!strcmp(token, "buy")) {
            int buy_id = atoi(strtok(NULL, delim));
            int buy_amount = atoi(strtok(NULL, delim));
            int r = change_stock(buy_id, 'b', buy_amount);

            if (r == 0) {
                sprintf(response, "[buy] success\n");
            } else if (r == 1) {
                sprintf(response, "Not enough stock\n");
            } else if (r == -1) {
                sprintf(response, "Invalid ID\n");
            }
            Rio_writen(connfd, response, MAXLINE);
        } else if (!strcmp(token, "sell")) {
            int sell_id = atoi(strtok(NULL, delim));
            int sell_amount = atoi(strtok(NULL, delim));
            int r = change_stock(sell_id, 's', sell_amount);

            if (r == 0) {
                sprintf(response, "[sell] success\n");
            } else if (r == -1) {
                sprintf(response, "Invalid ID\n");
            }
            Rio_writen(connfd, response, MAXLINE);
        } else if (!strcmp(token, "exit\n")) {    
            /* clinet connection 종료시켜야 함*/   
            //printf("received [exit] command\n");
            //Rio_writen(connfd, "[exit]", MAXLINE);
            break;
        } else {
            sprintf(response, "Unknown command\n");
            Rio_writen(connfd, response, MAXLINE);
        }
    }
}

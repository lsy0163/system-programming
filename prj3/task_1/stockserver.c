/*
 * stockserver.c - A level-order binary tree based stock server
 *
 * server ip: 172.30.10.11 (cspro)
 * port: 60029
 */
#include "csapp.h"
#include <time.h>

static struct timespec first_connect = {0}, last_disconnect = {0};
static int clientcnt = 0; /* number of active clients */

#define MAX_STOCK_NUM 1024

/* Stock databse encapsulation */
typedef struct node {
    int ID;
    int left_stock;
    int price;
    struct node *left, *right;
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

/* server pool */
typedef struct { /* Represents a pool of connected descriptors */
    int maxfd;                      /* Largest descriptor in read_set */
    fd_set read_set;                /* Set of all active descriptors */
    fd_set ready_set;               /* Subset of descriptors ready for reading */
    int nready;                     /* Number of ready descriptors from select */
    int maxi;                       /* High water index into client array */
    int clientfd[FD_SETSIZE];       /* Set of active descriptors */
    rio_t clientrio[FD_SETSIZE];    /* Set of active read buffers */
} pool;

/* server pool operations */
static void init_pool(int listenfd, pool *p);
static void add_client(int connfd, pool *p);
static int all_closed(pool *p);
static void close_client(pool *p, int i);
static void handle_request(pool *p, int connfd, char *buf, int idx);
static void check_clients(pool *p);

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }

    Signal(SIGINT, sigint_handler);
    load_stock("stock.txt");

    int listenfd = Open_listenfd(argv[1]);
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    char client_host[MAXLINE], client_port[MAXLINE];
    static pool pool;

    init_pool(listenfd, &pool);

    while (1) {
        pool.ready_set = pool.read_set;
        pool.nready    = Select(pool.maxfd+1,
                                &pool.ready_set, NULL, NULL, NULL);
        if (FD_ISSET(listenfd, &pool.ready_set)) {
            clientlen = sizeof(clientaddr);
            int connfd = Accept(listenfd,
                                (SA*)&clientaddr, &clientlen);
            Getnameinfo((SA*)&clientaddr, clientlen,
                        client_host, sizeof(client_host),
                        client_port, sizeof(client_port), 0);
            printf("Connected to (%s, %s)\n", client_host, client_port);
            add_client(connfd, &pool);
        }
        check_clients(&pool);
    }
    return 0;
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
    if (!f) { perror("fopen"); exit(1); }
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
    if (req == 'b') {
        if (n->left_stock < amt) return 1; /* Not enough stock*/
        n->left_stock -= amt;
    } else if (req == 's') { /* sell is always success */
        n->left_stock += amt;
    }
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

/*-------------- Pool management --------------*/
/* Initialize the pool of active clients */
static void init_pool(int listenfd, pool *p) {
    /* Initially, there are no connected descriptors */
    p->maxi = -1;
    for (int i = 0; i < FD_SETSIZE; i++) 
        p->clientfd[i] = -1;
    
    /* Initially, listenfd is only member of select read set */
    p->maxfd = listenfd;
    FD_ZERO(&p->read_set);
    FD_SET(listenfd, &p->read_set);
}

/* Add a new client connection to the pool */
static void add_client(int connfd, pool *p) {
    int i;
    p->nready--;
    for (i = 0; i < FD_SETSIZE; i++) { /* Find an available slot */
        if (p->clientfd[i] < 0) {
            /* Add connected descriptor to the pool */
            p->clientfd[i] = connfd;
            Rio_readinitb(&p->clientrio[i], connfd);

            /* Add the descriptor to descriptor set */
            FD_SET(connfd, &p->read_set);

            /* Update max descriptor and pool high water mark */
            if (connfd > p->maxfd)
                p->maxfd = connfd;
            if (i > p->maxi)
                p->maxi = i;

            clientcnt++;
            if (clientcnt == 1 && first_connect.tv_sec == 0) {
                clock_gettime(CLOCK_MONOTONIC, &first_connect);
            }

            break;
        }
    }
    if (i == FD_SETSIZE) /* Couldn't find an empty slot */
        app_error("add_client error: Too many clients");
}

/* Check if all clients are close */
static int all_closed(pool *p) {
    for (int i = 0; i <= p->maxi; i++)
        if (p->clientfd[i] >= 0) return 0; /* Not all close */
    return 1; /* All clients are closed */
}

/* Close a client connection and removes it from the pool */
static void close_client(pool *p, int i) {
    Close(p->clientfd[i]);
    FD_CLR(p->clientfd[i], &p->read_set);
    p->clientfd[i] = -1;

    clock_gettime(CLOCK_MONOTONIC, &last_disconnect);
    clientcnt--;

    if (clientcnt == 0) {
        if(first_connect.tv_sec != 0) {
            double elapsed = (last_disconnect.tv_sec - first_connect.tv_sec) + (last_disconnect.tv_nsec - first_connect.tv_nsec) / 1e9;
            printf(">> elapsed time: %.6f\n", elapsed);
        }
    }

    if (all_closed(p)) /* All clients are closed */
        dump_stock("stock.txt");
}

/* Handle a clients' request */
static void handle_request(pool *p, int connfd, char *buf, int idx) {
    char cmd[16];
    int id, amt;
    buf[strcspn(buf, "\n")] = '\0';
    char response[MAXLINE]; /* server's response to client */
    memset(response, '\0', sizeof(char) * MAXLINE);
    int nargs = sscanf(buf, "%15s %d %d", cmd, &id, &amt);

    if (nargs >= 1 && strcmp(cmd, "show") == 0) {
        size_t n = list_stock();
        Rio_writen(connfd, db.buf, MAXLINE);
    } else if (nargs == 3 && strcmp(cmd, "buy") == 0) {
        int r = change_stock(id, 'b', amt);

        if (r == 0) {
            sprintf(response, "[buy] success\n");
        } else if (r == 1) {
            sprintf(response, "Not enough stock\n");
        } else if (r == -1) {
            sprintf(response, "Invalid ID\n");
        }

        Rio_writen(connfd, response, MAXLINE);
    } else if (nargs == 3 && strcmp(cmd, "sell") == 0) {
        int r = change_stock(id, 's', amt);
        
        if (r == 0) {
            sprintf(response, "[sell] success\n");
        } else {
            sprintf(response, "Invalid ID\n");
        }
        
        Rio_writen(connfd, response, MAXLINE);
    } else if (nargs >= 1 && strcmp(cmd, "exit") == 0) {
        close_client(p, idx);
    } else {
        sprintf(response, "Unknow command\n");
        Rio_writen(connfd, response, MAXLINE);
    }
}

/* Service ready client connections */
static void check_clients(pool *p) {
    int i, connfd, n;
    char buf[MAXLINE];
    rio_t rio;
    for (i = 0; (i <= p->maxi && p->nready > 0); i++) {
        connfd = p->clientfd[i];
        rio = p->clientrio[i];

        /* If the descriptor is ready, echo a text line from it */
        if ((connfd > 0) && FD_ISSET(connfd, &p->ready_set)) {
            p->nready--;
            n = Rio_readlineb(&rio, buf, MAXLINE);
            if (n > 0) { /* Read a line from the client */
                printf("server received %d bytes\n", n);
                handle_request(p, connfd, buf, i);
            } else { /* EOF detached */
                close_client(p, i);
            }
        }
    }
}
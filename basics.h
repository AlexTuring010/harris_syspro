#ifndef FSS_UTILS_H
#define FSS_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> //για read close
#include <fcntl.h> //για το open
#include <errno.h> //για το errno
#include <sys/stat.h> // για το mkfifo
#include <sys/wait.h>
#include <time.h>
#include <dirent.h>
#include <sys/select.h>
#include <sys/types.h>// για το inotify
#include <sys/inotify.h> //για το inotify
#include <signal.h> //για το sigchld_handler
#include <dirent.h>// για το readdir
#include <stdarg.h>  //για va_list, va_start, va_end
#include <ctype.h>   //για isprint
#define BUFSIZE 4096 //μεγιστο μηκος buffer
#define MAX_LINE 1024 //μεγιστο μηκος γραμμης
#define FIFO_IN "fss_in" // επικοινωνια με το worker
#define FIFO_OUT "fss_out" //επιστροφη στον manager
#define HASH_SIZE 211 //μεγεθος hash table
#define RESPONSE_SIZE (MAX_LINE * 4) //μεγιστο μεγεθος απαντησης

//Για την 2η εργαια includes 
#include <netdb.h> //για το getaddrinfo
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <pthread.h>

typedef struct SyncEntry //για την καταχωρηση του hash table
{
    char source[MAX_LINE];
    char target[MAX_LINE];
    int active;
    int wd;
    struct SyncEntry *next;
} SyncEntry;

//δομη job για συγχρονισμο αρχειων
typedef struct {
    char source_dir[MAX_LINE];     
    char target_dir[MAX_LINE];     
    char source_host[64];          
    char target_host[64];          
    int source_port;               
    int target_port;               
    char filename[MAX_LINE];       //αρχειο προς συγχρονισμο
} job_t;


void sigchld_handler(int signo);// χειριστης για το SIGCHLD

extern SyncEntry *sync_list;

extern SyncEntry *hash_table[HASH_SIZE];

typedef struct queue_stats //για την ουρα
{
    char source[MAX_LINE];
    char target[MAX_LINE];
    char filename[MAX_LINE];
    char operation[32];
    struct queue_stats *next;
} queue_stats;

extern queue_stats *queue_head;//αρχικο σημειο της ουρας
extern queue_stats *queue_tail;//τελικο σημειο της ουρας

unsigned int hash_function(const char *input); // υπολογισμος hash value
SyncEntry *find_node(int wd);//αναζητηση wd στο hash table
void insert_node(const char *source, const char *target);//προσθηκη νεου node στο hash table
SyncEntry *find_node_source(const char *source);// αναζητηση source στο hash table
void set_node_inactive(const char *source);//απενεργοποιηση node
void print_list();//εκτυπωση του hash table
void free_list();//απελευθερωση μνημης του hash table
void get_timestamp(char *buffer, size_t size);// current timestamp
void init_queue();// αρχικοποιηση ουρας queue
void enqueue(const char *source, const char *target, const char *filename, const char *operation);// προσθηκη νεου node στην ουρα
queue_stats *dequeue();//αφαιρεση node απο την ουρα
int is_empty();//ελεγχος αν η ουρα ειναι αδεια

#endif